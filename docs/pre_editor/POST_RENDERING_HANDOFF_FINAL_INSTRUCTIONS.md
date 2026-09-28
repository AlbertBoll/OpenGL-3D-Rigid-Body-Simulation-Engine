# Post-Rendering Handoff Audit and PRE_EDITOR Planning

**Suggested repository location:**  
`docs/pre_editor/POST_RENDERING_HANDOFF_FINAL_INSTRUCTIONS.md`

**Task type:** audit and planning only.  
**PRE_EDITOR remains:** `PROPOSED / NOT_STARTED`.

START THE POST-RENDERING HANDOFF AUDIT AND PRE-EDITOR PLANNING TASK.

Perform this task only after Rendering Phase 68 has been fully approved, committed, tagged, published, and recorded as complete by the authoritative Rendering workflow.

The purpose of this task is to establish exactly what the completed Rendering Refactor provides, audit the remaining Engine foundations required before Core Editor, and prepare one unified PRE_EDITOR phase series.

The intended high-level sequence is:

Rendering Phase 68 final approval/publication  
→ Post-Rendering / Engine Handoff Audit  
→ reviewed PRE_EDITOR plan  
→ separately authorized PRE_EDITOR activation/minimal setup  
→ Code Quality Baseline phases  
→ evidence-required pre-A remediation only where justified  
→ **A — Resource/API Consolidation + Shader / Material / Geometry Authoring Foundation**  
→ **B — Event / Input Refactor and Optimization + Camera Core System**  
→ **C — Physics Core / API / Rendering Integration / Constraints / Ragdoll**  
→ **D — Lighting System and PBR/HDR Pipeline**  
→ **E — Engine Authoring Foundation: Scene/ECS / Project / Persistence / Transactions**  
→ **F — Final Integration and Core Editor Handoff**  
→ Core Editor Refactor

This request authorizes:

- read-only current-source inspection;
- evidence gathering;
- finding classification;
- bounded, non-mutating diagnostics within documented existing authority and Section 1.1;
- inactive governance/planning artifacts;
- proposed phase contracts.

It does NOT authorize implementation of any proposed PRE_EDITOR phase.

All A-F milestone requirements, implementation verbs, and future validation gates below describe outcomes to be proposed for later separately authorized phases.

During this task:

- inspect the current source;
- classify findings;
- define dependencies;
- prepare phase contracts only.

`RigidBodySimulation` is the sole application for new end-to-end runtime, visual, performance, and human-acceptance validation for this audit and PRE_EDITOR.

Apply the minimal validation policy in Section 1.2.

Relevant A-E capabilities must eventually be exercised through the actual maintained `RigidBodySimulation` consumer path rather than only:

- isolated probes;
- substitute demos;
- successful compilation;
- simple startup smoke tests.

---

# 1. Authorization and hard stop conditions

Read and obey all applicable repository instructions and existing workflow rules.

Treat this as a natural-language audit/planning request.

Do not dispatch an unrelated existing START command.

First prove from Git and authoritative workflow records that Rendering Phase 68 is:

- approved;
- committed;
- tagged;
- branch-published;
- tag-published;
- recorded as completed;
- free of unfinished approval/publication operations.

A tag alone is insufficient evidence.

If this cannot be proven, STOP and report the exact missing evidence.

Do not attempt to finish Phase 68 yourself.

After Phase 68 verification, treat its approved checkpoint as the immutable Rendering historical reference baseline.

Do NOT during this audit:

- create Rendering Phase 69;
- reopen Rendering history;
- renumber Rendering history;
- modify production code;
- modify shaders;
- modify assets;
- modify tests;
- modify build configuration;
- change dependencies;
- change compiler/CRT/language policy;
- change Physics behavior;
- change application behavior;
- perform cleanup;
- perform formatting;
- mass rename;
- stage;
- commit;
- tag;
- push;
- reset;
- stash;
- clean;
- switch worktrees;
- manufacture receipt/seal/checkpoint/approval state;
- activate PRE_EDITOR;
- start any implementation phase.

Preserve:

- index state;
- unrelated tracked work;
- unrelated untracked work;
- protected files;
- sealed artifacts.

Do not modify live:

- `AGENTS.md`;
- workflow skills;
- command routing;
- validators;
- publication policy;
- repository-root `.clang-format`;
- canonical coding-style policy.

Necessary changes to these may only be proposed as inactive PRE_EDITOR artifacts.

Work autonomously inside this audit authorization.

Do not repeatedly ask whether to continue already-authorized inspection.

Record genuinely new owner decisions for final review.

Do not spawn additional agents unless separately authorized.

## 1.1 Audit-time interpretation and diagnostic boundaries

Use source inspection and existing approved evidence first.

Record the following for any new diagnostic run:

- authority;
- purpose;
- baseline identity;
- expected output;
- bounded execution limit.

An existing executable or diagnostic tool may be run only when:

- the specific operation is covered by documented existing authority;
- the inspected executable/tool and its input provenance are identified;
- the run requires no source, shader, test, asset, build-configuration, or live workflow changes;
- logs, captures, caches, and other generated data can be directed to a separate unsealed evidence location;
- sealed binaries, libraries, manifests, snapshots, reference images, and existing evidence remain unchanged;
- the run complies with Section 1.2;
- the run stops at its declared limit.

This request does not itself authorize:

- recompilation;
- new probes;
- instrumentation changes;
- new benchmark harnesses;
- broad benchmark campaigns.

Do not rebuild into, overwrite, reseal, or republish the Phase 68 checkpoint to obtain fresh measurements.

If a required diagnostic would exceed existing authority:

- record the smallest concrete diagnostic proposal;
- record its isolated output location;
- record required changes;
- record expected evidence;
- record the affected gate;
- mark the dependent conclusion `EVIDENCE_INSUFFICIENT` or `BLOCKED` as appropriate;
- continue independent authorized inspection.

Future diagnostic build proposals must isolate all generated outputs from the sealed checkpoint.

Isolation alone does not authorize the build or its code changes.

Do not ask whether to continue ordinary read-only inspection already authorized here.

Group genuinely new authority decisions for final owner review unless the Phase 68 entry gate itself cannot be proven.

## 1.2 Minimal validation policy

Use `RigidBodySimulation` for all new:

- application-level execution;
- visual comparisons;
- performance measurements;
- human acceptance.

Do not automatically run:

- Editor;
- Breakout;
- RayTracing;
- substitute demos;
- the complete historical application matrix.

For other maintained applications:

- inspect affected public interfaces;
- inspect compatibility obligations.

Future compile/link checks must be justified by:

- an affected shared boundary;
- or an existing mandatory gate.

They are not a standing authorization for additional application runs.

Use existing focused tests only when they address:

- a concrete affected invariant;
- a concrete failure mode;
- a required repository gate.

Future phases may propose or implement focused tests under their own approved scope.

Do not add tests during this audit.

Reuse valid evidence with matching:

- source identity;
- build identity;
- input identity.

Do not repeat an unaffected test merely because another milestone exists.

New implementation evidence must exercise the actual maintained `RigidBodySimulation` consumer path where relevant.

Focused tests supplement that evidence.

Compilation and startup alone do not establish end-to-end correctness.

Preserve existing mandatory gates.

If a gate conflicts with this application scope:

- identify the exact conflict for owner resolution;
- do not silently waive it;
- do not silently broaden the workload.

Stop verification when:

- the relevant contract's required evidence is sufficient;
- or a declared failure/stop condition is reached.

Do not expand testing until favorable results appear.

---

# 2. Progressive disclosure and token control

Progressive disclosure is mandatory.

Token reduction must not weaken:

- architecture coverage;
- ownership analysis;
- validation;
- finding traceability;
- approval gates;
- correctness conclusions.

Initially load only:

- applicable repository instructions;
- compact Rendering workflow state;
- Phase 68 receipt/checkpoint;
- approval/publication records;
- completion criteria;
- relevant validation policy;
- architecture/code-map locators.

Read this master instruction for the initial audit/planning task.

Record stable section references in the resulting compact manifest so subsequent PRE_EDITOR phases do not routinely reload:

- the full master instruction;
- the full roadmap.

Do not initially load:

- every Rendering phase contract;
- every historical review;
- all historical audits;
- all benchmark logs;
- the entire source tree.

Use targeted:

- `rg`;
- `rg --files`;
- read-only Git inspection;
- symbol search;
- path search.

Locate code before opening large files.

Inspect enough surrounding:

- implementation;
- callers;
- ownership paths;
- validation fixtures;
- dependency boundaries;

to establish actual semantics.

Architecture summaries are locators, not proof.

An isolated declaration/search hit is not sufficient architecture evidence.

Sampling may discover a problem but does not prove an engine-wide invariant.

Maintain one canonical finding record per issue.

Use stable finding IDs.

Maintain a compact coverage/resume record.

Mark topics explicitly as:

- `VERIFIED`;
- `GAP_FOUND`;
- `EVIDENCE_INSUFFICIENT`;
- `NOT_APPLICABLE_WITH_REASON`;
- `BLOCKED`.

Future PRE_EDITOR execution must also use progressive disclosure.

START/REVISE normally loads only:

- current PRE_EDITOR state;
- applicable execution instructions;
- active phase contract;
- relevant validation rules;
- targeted source/code-map context.

APPROVE should use compact:

- receipt verification;
- seal verification;
- identity verification;
- evidence verification;

rather than routinely rereading:

- the full roadmap;
- historical phase contracts;
- source tree;
- reviews;
- logs.

A mismatch, missing evidence, policy requirement, or requested revalidation still follows the established workflow.

Token reduction never waives a gate.

---

# 3. Establish exact baselines

Record separately:

| Identity | Meaning |
|---|---|
| Rendering historical reference | Approved Phase 68 commit/tag and evidence; immutable |
| PRE_EDITOR entry baseline | Phase 68 unless a later explicitly approved corrective checkpoint becomes the authorized entry |
| Current worktree | Actual HEAD/local changes; not automatically an approved baseline |
| Physics authoritative baseline | Physics code actually used by the final maintained application |
| Physics refactor/experimental baseline | Separate worktree/branch/experimental state; evidence only unless separately approved |

For Rendering record:

- repository;
- branch;
- Phase 68 tag;
- full resolved commit SHA;
- publication destination;
- branch publication completion;
- tag publication completion;
- approval bookkeeping;
- HEAD;
- index;
- tracked differences;
- untracked differences;
- protected local work;
- compiler/toolchain;
- Debug/Release configurations;
- maintained applications;
- validation evidence locations.

Use read-only Git inspection if the worktree differs.

Do not:

- reset;
- stash;
- discard;
- hide local work;

to manufacture a clean baseline.

For Physics determine:

- authoritative Physics source used by `RigidBodySimulation`;
- Physics-refactor worktree/branch identity if present;
- approved versus experimental changes;
- unresolved findings;
- reverted/rejected experiments;
- relevant benchmark/test evidence.

Experimental Physics work is not automatically approved merely because it exists in another worktree.

---

# 4. Rendering completion/residue audit

Inspect the approved Phase 68 source for:

- obsolete Rendering paths;
- temporary adapters;
- duplicate old/new APIs;
- legacy manager ownership;
- native OpenGL/GLAD/SDL leakage;
- RawModel/ShapeManager legacy paths where removal was required;
- shader compatibility layers;
- material compatibility layers;
- mesh compatibility layers;
- transitional exception/error adapters;
- duplicated frame/pass orchestration;
- lifetime ambiguity;
- publication ambiguity;
- shutdown ambiguity.

Verify relevant final Rendering invariants:

- generation/version identity;
- context-thread ownership;
- immutable RenderFrame;
- retained frame-visible resources;
- publication/extraction boundaries;
- backend isolation;
- public error boundary;
- final exception policy.

Classify each finding as:

1. `RENDERING_COMPLETION_DEFECT`;
2. explicitly accepted Rendering legacy/interop/deferral;
3. intentional PRE_EDITOR capability gap;
4. intentional Core Editor capability gap.

Never silently move a Rendering completion defect into PRE_EDITOR simply to allow handoff.

Approved Rendering history remains immutable.

If post-approval correction is genuinely required, propose a separately authorized corrective checkpoint.

Do not rewrite approved Rendering history.

---

# 5. Cross-cutting API complexity and ownership audit

Audit not only individual APIs but the dependency structure normal callers must understand.

Trace representative operations including:

- create primitive mesh;
- create standard material;
- modify standard material;
- assign mesh/material;
- create/configure custom shader/material;
- share mesh;
- duplicate Entity retaining shared mesh;
- explicit mesh copy;
- MakeUnique;
- resize/stretch object;
- create Scene Camera;
- use Editor Camera/view;
- create Directional Light;
- create Point Light;
- create Spot Light;
- subscribe/unsubscribe events;
- consume input;
- publish/replace resources;
- create rigid body;
- create collider;
- synchronize Physics and rendering transforms;
- create a basic Physics constraint where current architecture permits.

For each representative operation record:

- public entry points;
- relevant paths;
- relevant symbols;
- approximate explicit call count;
- required headers;
- required managers/services;
- required registries;
- ownership assumptions;
- update/publication assumptions;
- backend knowledge required;
- reflection/string knowledge required;
- repeated boilerplate.

The goal is not mechanically minimizing call count.

The goal is:

- low cognitive overhead;
- explicit cost;
- explicit failure;
- explicit ownership;
- preserved advanced APIs;
- no hidden hot-path work.

Convenience APIs must not hide repeated:

- shader compilation;
- model import;
- heavy copies;
- GPU uploads;
- registration;
- synchronization;
- publication;
- reflection.

---

# 6. Mandatory Manager / Resource / Service Boundary Audit

This is a required audit area.

At minimum inspect:

- `AssetManager`;
- `ShapeManager`;
- `ShaderManager`;
- Material managers/services;
- Texture/resource managers;
- asset registries;
- `FrameResource` or equivalent;
- `EngineContext`;
- resource update/publication services.

For every relevant manager/service determine:

- what it owns;
- what it creates;
- what it loads/imports;
- what it caches;
- what it registers;
- what it resolves;
- what it publishes;
- what GPU/update work it initiates;
- what it destroys/retires;
- its error model;
- its thread assumptions;
- allowed callers;
- overlap with other services.

Do not preserve a manager merely because it already exists.

Do not split/delete a manager merely for aesthetics.

Base changes on:

- ownership;
- lifetime;
- API complexity;
- error model;
- dependency direction;
- reuse evidence.

## 6.1 AssetManager

Explicitly distinguish:

- runtime asset identity/lookup;
- CPU load/import;
- runtime cache;
- runtime lifetime;
- GPU-safe publication/replacement;
- future persistent `AssetDatabase`.

Do NOT prematurely implement Milestone E's persistent AssetDatabase inside AssetManager.

If responsibilities are currently mixed, record the ownership split required.

## 6.2 ShapeManager

Determine whether ShapeManager remains:

- a legitimate runtime service;
- or an obsolete/parallel primitive/GPU path.

Milestone A should converge ordinary primitive creation on:

Geometry Template  
→ MeshAsset  
→ MeshHandle  
→ approved publication/upload architecture.

Do not retain a second ShapeManager-owned mesh/GPU architecture solely for compatibility unless explicitly justified.

## 6.3 ShaderManager

Audit ShaderManager specifically for:

- source/description ownership;
- compile/link responsibility;
- ShaderProgram creation;
- identity/lookup;
- caching;
- lifetime;
- reflection;
- reload/replacement;
- diagnostics;
- native program access;
- error transport;
- service lookup.

Ordinary Material users must not need direct ShaderManager knowledge merely to use standard engine materials.

Do not solve ShaderManager complexity by wrapping the same god-object behind `Materials::Standard()`.

If responsibilities require separation, propose the smallest bounded split justified by actual dependency/ownership evidence.

## 6.4 FrameResource

Determine whether FrameResource is incorrectly exposed to application/scene/material/geometry callers.

Normal authoring should conceptually follow:

Authoring state change  
→ revision/resource change  
→ safe update/publication boundary  
→ renderer-owned frame-resource machinery.

Do not design a prettier public FrameResource API merely to preserve current leakage.

Keep renderer-private frame-resource machinery private unless a genuine semantic public responsibility is demonstrated.

## 6.5 EngineContext / service lookup

Audit deep lookup chains such as:

EngineContext  
→ Manager  
→ Registry  
→ FrameResource.

Inspect:

- service-locator coupling;
- manager-to-manager dependencies;
- dependency cycles;
- global/current-context reliance;
- ordinary consumer dependency depth.

Do not replace every service with global free functions.

Prefer explicit ownership and narrow semantic entry points.

Milestone A must pass both:

1. **Public API simplicity gate**
2. **Internal dependency-direction gate**

A façade over unchanged cyclic/global manager spaghetti does not satisfy Milestone A.

---

# 7. Code Quality Baseline audit

Inspect likely PRE_EDITOR touchpoints for:

- excessively long lines;
- multiple significant statements on one line;
- unreadable conditions;
- excessive nesting;
- oversized functions;
- mixed responsibilities;
- inconsistent naming;
- ambiguous ownership naming;
- public-header coupling;
- implementation leakage;
- duplicated helpers;
- duplicated boilerplate;
- poor comments;
- comments that only repeat code;
- unreadable `std::expected` propagation.

Classify findings as:

- `MUST_FIX_BEFORE_A`;
- `FIX_WHEN_TOUCHED`;
- `DEFER`.

Do not make the entire repository style-clean.

Prepare inactive proposals for:

- `.clang-format`;
- coding-style documentation.

The proposed style should cover:

- consistent indentation;
- brace policy;
- approximately 100-column target;
- approximately 120-column practical upper guidance;
- one significant action per line;
- readable conditions;
- naming;
- include discipline;
- ownership-oriented naming;
- expected/error formatting;
- comments;
- function responsibility.

Do not create or replace the live repository-root `.clang-format` now.

Do not repository-wide format.

Separate future work into:

1. policy adoption;
2. mechanical formatting;
3. semantic readability cleanup;
4. API redesign;
5. performance work.

Formatting is not proof of behavioral equivalence.

---

# 8. Rendering performance residual audit

Use approved evidence from:

- Phase 13;
- Phase 48;
- Phase 65;
- Phase 68;
- `POST_RENDERING_PERFORMANCE_AUDIT.md` or canonical successor.

Carry forward mandatory unresolved findings including:

- C-scaling 5% run-median-spread qualification;
- Phase 13 historical absolute CPU closure;
- async texture-publication stall remediation;
- GPU/UI/shadow-tail reproducibility/remediation;
- remaining historical attribution/performance uncertainties.

Do not reinterpret unresolved status as automatic Phase 65/68 failure.

For each residual record:

- canonical ID;
- original version;
- current inspected version;
- evidence;
- technical status;
- known facts;
- unknown facts;
- owner;
- affected gate;
- next minimal diagnostic;
- measurable closure condition;
- actual acceptance if any.

Classify as:

- `RENDERING_COMPLETION_DEFECT`;
- `PRE_A_PERFORMANCE_REMEDIATION`;
- milestone-owned optimization;
- accepted nonblocking residual;
- optional optimization.

Do not optimize during this audit.

Run only bounded diagnostics already covered by applicable authority.

Do not extend tests until favorable numbers appear.

---

# 9. Milestone A — Resource/API Consolidation + Shader / Material / Geometry Authoring Foundation

Milestone A must establish simple authoring APIs without creating a second runtime architecture.

## A.1 Resource/Manager consolidation

Use Section 6 findings to remove ordinary caller dependence on manager chains.

Normal authoring must not require knowledge of:

- AssetManager internals;
- ShapeManager internals;
- ShaderManager internals;
- FrameResource;
- renderer-private registries;
- GPU publication details.

## A.2 Shader authoring

Assess:

- parameter declarations;
- semantic bindings;
- reflection validation;
- common schemas;
- bounded variants/permutations;
- diagnostics;
- explicit reload;
- safe replacement;
- last-valid retention;
- retained old-frame validity.

Automatic file watching is not required before explicit reload works correctly.

## A.3 Mandatory Geometry Template API

Provide a simple engine-owned authoring path conceptually equivalent to:

```cpp
auto cube = Geometry::Cube();

auto sphere = Geometry::Sphere({
    .segments = 32,
    .rings = 16
});
```

Exact APIs must come from source-based design.

At minimum evaluate:

- Cube;
- Plane;
- Sphere;
- Cylinder;
- Capsule;
- Cone;
- Quad/Grid where justified.

Reuse:

- MeshAsset;
- MeshHandle;
- approved registry/publication/upload architecture.

Define:

- canonical/shared geometry;
- duplicate Entity sharing;
- explicit copy;
- MakeUnique;
- geometry-generation parameters;
- Transform scale;
- non-uniform scale;
- dimensions;
- stretch/resize;
- Apply/Bake;
- bounds;
- revisions.

Explicitly distinguish:

Transform/object change  
≠ primitive regeneration  
≠ MeshAsset/topology modification.

Mesh Edit Mode and topology editing remain later work.

## A.4 Mandatory Material Template API

Provide simple standard material creation conceptually equivalent to:

```cpp
auto material = Materials::Standard({
    .baseColor = Color::White,
    .metallic = 0.0f,
    .roughness = 0.5f
});
```

Evaluate standard templates equivalent to:

- Standard PBR;
- Unlit;
- Masked;
- Transparent;
- Debug/reference material.

Ordinary callers must not manually:

- construct PipelineState;
- query ShaderManager;
- inspect native GPU programs;
- query uniform locations;
- reproduce reflection;
- coordinate registry publication.

Expose semantic properties such as:

- BaseColor;
- Metallic;
- Roughness;
- Normal;
- Emissive;
- Alpha;
- AlphaCutoff;
- standard texture/sampler semantics.

Preserve advanced APIs for custom rendering.

## A.5 Milestone A gate

Prove an ordinary caller can:

- create primitives;
- create standard material;
- assign mesh/material;
- modify standard parameters;
- share geometry;
- duplicate Entity with sharing;
- MakeUnique;
- resize/stretch without topology editing;

without backend knowledge or manager spaghetti.

Correctness alone is insufficient.

API simplicity and internal dependency direction are both required.

---

# 10. Milestone B — Event / Input Refactor and Optimization + Camera Core

## B.1 Event/Input

Audit and plan:

- platform → engine typed event boundary;
- keyboard;
- text input;
- pointer;
- mouse buttons;
- wheel;
- modifiers;
- focus;
- capture;
- handled/consumed state;
- routing order;
- cancellation;
- subscription lifetime;
- RAII subscription release;
- reentrancy;
- subscription mutation during dispatch;
- synchronous delivery;
- queued/deferred delivery;
- thread affinity;
- safe dispatch points;
- stale-event rejection;
- async completion handoff.

Do not create a universal event scheduler.

Do not route:

- frame sequencing;
- render-pass execution;
- Physics stepping;
- resource publication;
- explicit subsystem commands;

through a generic EventBus without demonstrated need.

Distinguish:

- lossless transitions;
- coalescible latest-state events;
- notifications;
- commands/mutations.

Mutation occurs through authoritative APIs/transactions.

Notifications report successful state changes.

Establish Input State for:

- held;
- pressed;
- released;
- pointer;
- delta;
- wheel;
- modifiers.

Measure before optimizing:

- dispatch cost;
- input-to-consumer latency;
- allocations;
- copies;
- listener lookup;
- queue backlog;
- synchronization;
- high-frequency event paths.

## B.2 Camera Core

Establish distinct:

- Scene Camera;
- Editor Camera.

Assess:

- projection;
- frustum;
- view/projection matrices;
- inverse matrices;
- viewport binding;
- active Scene Camera;
- preview;
- switching;
- deletion/replacement;
- aspect;
- DPI;
- RenderFrame snapshots.

Unify Camera data for:

- rendering;
- culling;
- shadows;
- picking;
- screen/world conversion.

Support capability equivalent to:

- World → View;
- World → NDC;
- World → viewport pixel;
- viewport pixel → ray.

Do not implement Core Editor selection/gizmo UI here.

---

# 11. Milestone C — Physics Core / API / Rendering Integration / Constraints / Ragdoll

Physics is a first-class PRE_EDITOR milestone.

It is not limited to bug fixing.

Milestone C must address:

- correctness;
- stability;
- performance;
- API simplicity;
- Scene/Transform integration;
- Rendering/presentation integration;
- collision-shape API;
- multi-rigid-body constraints;
- Ragdoll foundation.

## C.1 Physics baseline/provenance

Determine:

- authoritative current Physics source;
- Physics-refactor worktree/branch;
- experiments;
- approved/rejected/reverted changes;
- benchmarks/tests;
- unresolved findings.

Do not silently merge experimental work.

## C.2 Known stability findings

Explicitly audit the sphere-lattice settling issue:

Upper-layer spheres may continue weak bouncing after landing instead of converging cleanly toward stable rest.

Treat residual impulse as a symptom/hypothesis, not a predetermined root cause.

Inspect evidence for:

- restitution;
- low-speed restitution threshold;
- normal impulse accumulation;
- friction impulse accumulation;
- warm starting;
- manifold persistence;
- contact generation stability;
- solver passes;
- penetration correction/bias;
- fixed timestep;
- sleep/wake;
- island/contact graph;
- floating-point convergence.

Distinguish:

- genuine energy injection;
- insufficient convergence;
- unstable contact/manifold behavior;
- sleep-policy failure;
- bounded numerical residual;
- another demonstrated mechanism.

Do not “fix” the symptom only by raising sleep thresholds.

Do not simply increase solver iterations without measuring resulting cost.

## C.3 Physics correctness scenarios

At minimum assess:

- sphere lattice;
- sphere-on-ground settling;
- box stacking;
- frictional rest;
- low-speed impacts;
- multi-contact;
- spin/rotation settling;
- fixed-step sensitivity;
- sleeping/wake;
- constraint chains once available;
- Ragdoll settling once available.

Record quantitative evidence where possible:

- linear velocity;
- angular velocity;
- drift;
- penetration;
- contacts;
- impulses;
- sleep/wake churn;
- active-body count;
- solver iterations.

`RigidBodySimulation` is mandatory application-level Physics evidence.

## C.4 Physics performance

Assess:

- broad phase;
- narrow phase;
- contact generation;
- contact graph/islands;
- solver;
- solver-pass scaling;
- sleeping savings/overhead;
- active-body scaling;
- contact-count scaling;
- transient allocations;
- repeated work.

Correctness and performance findings remain separate.

Future Physics performance phases must use the predeclared validation and acceptance protocol in Section 18.2.

Do not define, relax, replace, or reinterpret Physics performance thresholds after observing candidate results.

If required numeric thresholds are not yet justified by approved criteria, baseline evidence, or an owner decision, record them as `OPEN_DECISION` under Section 18.2 rather than inventing convenient limits.

## C.5 Physics public API simplification

Audit normal use of:

- PhysicsScene/World;
- RigidBody;
- Collider;
- CollisionShape;
- mass/inertia;
- Physics material/friction/restitution;
- velocity;
- force;
- impulse;
- sleep policy;
- Constraint.

Ordinary gameplay/authoring code must not require solver internals.

Evaluate simple CollisionShape APIs equivalent to:

- Box;
- Sphere;
- Capsule;
- ConvexHull where supported.

Render Mesh and Collision Shape remain distinct concepts.

## C.6 Body semantics

Define clear behavior for:

- Static;
- Kinematic;
- Dynamic.

Clarify ownership of state and transform flow.

## C.7 Physics ↔ Scene ↔ Rendering contract

Establish the relationship among:

- Authoring Transform;
- current Simulation Transform;
- previous Simulation Transform;
- presentation/interpolated Transform;
- RenderFrame Transform.

Physics, Scene, Renderer, and Editor must not ambiguously compete for Transform ownership.

Preserve fixed-step and presentation interpolation semantics.

Resolve the minimal runtime Scene/Transform prerequisites using Section 18.1.

Milestone C must not depend on Milestone E's completed persistence or transaction implementation.

## C.8 Generic Constraint Foundation

Plan a reusable constraint architecture rather than ad-hoc special joints.

Build toward:

- generic constraint representation;
- effective-mass/Jacobian-equivalent formulation where justified;
- accumulated impulses;
- warm starting;
- constraint error/stabilization;
- deterministic ownership/lifetime.

Do not overgeneralize before real joints exercise the design.

## C.9 Constraint progression

Plan bounded phases that satisfy the required Physics evidence in Section 14 and the Ragdoll foundation in C.10.

Required Milestone C capabilities include:

- a representative chained-constraint scenario;
- hinge constraints and angular/hinge limits;
- the minimum joint capabilities needed by the required basic Ragdoll.

Map each required capability to an existing approved implementation or a proposed providing phase.

Reuse correct existing capabilities before proposing reimplementation.

A source-informed progression may include:

1. Distance constraint;
2. chained constraints;
3. point/ball-and-socket constraint;
4. hinge;
5. angular/hinge limits.

The exact progression may vary with the approved baseline, but it must deliver the required chain, hinge/limits, and basic Ragdoll evidence.

Motors/drive behavior remains optional.

Include it only when justified by an actual finding or owner requirement and explicitly included in an approved phase scope.

It is not an implicit prerequisite for passive Ragdoll settling.

Each required or explicitly approved optional stage requires:

- correctness;
- stability;
- performance evidence.

## C.10 Ragdoll foundation

Ragdoll should compose normal Physics primitives:

Rigid Bodies  
+ Collision Shapes  
+ Constraints  
+ Joint Limits  
+ Collision Filtering.

Do not create a separate Ragdoll solver.

Initial Ragdoll scope should cover:

- body hierarchy;
- body-to-skeleton/bone mapping description;
- capsule/box/sphere shapes;
- parent-child joints;
- limits;
- collision filtering;
- sleep/wake;
- stable settling;
- debug visualization.

Do NOT require yet:

- active ragdoll;
- animation blending;
- IK;
- muscles;
- physical-animation controller;
- get-up animation.

Animation↔Ragdoll blending remains later subsystem integration.

## C.11 Physics debug visualization

Plan semantic rendering for:

- colliders;
- AABBs;
- contact points;
- contact normals;
- center of mass;
- constraint anchors;
- joint axes;
- joint limits;
- sleeping bodies;
- islands.

Reuse the Engine Debug/Authoring Visualization path rather than issuing raw OpenGL.

If that path is absent or insufficient, plan its smallest shared semantic foundation before the first dependent Physics validation.

Milestone E extends the same foundation.

It must not introduce a competing debug-rendering path.

Apply Section 18.1.

## C.12 Physics milestone gate

Physics cannot be considered stable enough for constraint/ragdoll expansion while known core resting-contact instability remains unexplained.

The sphere-lattice finding must be:

- resolved with evidence;
- or explicitly accepted with quantified bounds and rationale appropriate to later constraint/ragdoll workloads.

Milestone C must validate:

- correctness;
- settling;
- constraints;
- Ragdoll;
- CPU scaling;
- API usability;
- Transform/Rendering integration.

---

# 12. Milestone D — Lighting System and PBR/HDR Pipeline

Milestone D must establish one coherent PBR Lighting and HDR display pipeline.

Directional, Point, and Spot Lights and their corresponding Directional, Point, and Spot Shadows are all required Milestone D capabilities.

**Required capability does not imply required reimplementation.**

Preserve and validate an existing correct implementation before proposing:

- replacement;
- redesign;
- a parallel path.

Audit and reuse correct existing implementations.

A capability absent from Phase 68 must be classified and planned.

Its absence is not automatically a Rendering completion defect.

Classify it against the actual approved Rendering completion contract.

Do not interpret current lack of support as permission to omit a required Light or Shadow.

Any exception requires:

- an explicit owner-approved scope amendment;
- the remaining technical gap recorded.

Area Lights and Area-Light Shadows remain intentionally deferred.

Directional, Point, and Spot Lights must not behave as unrelated shading systems.

They must contribute through one shared Material/PBR evaluation model with light-type-specific:

- direction;
- radiance;
- attenuation;
- cone behavior;
- per-light visibility/shadowing.

Physically Based Bloom belongs to the linear-HDR post-lighting/display pipeline.

Bloom is not part of the BRDF itself.

The intended architecture is conceptually:

Material Surface Data  
→ Shared PBR BRDF  
← Directional Light + Directional Shadow Visibility  
← Point Light + Point Shadow Visibility  
← Spot Light + Spot Shadow Visibility  
→ Direct PBR Lighting  
+ IBL / Indirect PBR  
+ Emissive / Sky as applicable  
→ Linear HDR Scene Color  
→ Physically Based Bloom  
→ Exposure / pre-exposure according to the adopted pipeline  
→ Tone Mapping  
→ Final Display / sRGB.

Exact exposure/Bloom ordering or pre-exposure strategy must be explicitly defined by the adopted implementation and remain mathematically and visually consistent.

Do not invent production symbols before source inspection.

## D.1 Shared PBR Material/Light contract

Establish one coherent metallic/roughness PBR model for the supported standard material path.

At minimum assess and define, where appropriate:

- BaseColor;
- Metallic;
- Roughness;
- Normal;
- AO/occlusion semantics;
- Emissive;
- GGX normal distribution;
- Smith geometry/masking-shadowing;
- Schlick Fresnel;
- diffuse/specular energy conservation;
- common normal-map/TBN behavior.

Directional, Point, and Spot Lights must reuse this common Material/BRDF logic.

Do not maintain separate incompatible PBR equations per Light type.

Light-specific code should primarily provide BRDF inputs such as:

- direction;
- radiance;
- distance attenuation;
- Spot cone attenuation;
- shadow visibility.

## D.2 Directional Light

Assess and establish:

- direction;
- color;
- intensity;
- units/semantics;
- enabled state;
- renderer/GPU representation;
- revision/update behavior;
- Directional Shadow participation.

Directional Light must enter the shared Direct PBR evaluation.

Directional Shadow must act as visibility for the Directional Light contribution rather than incorrectly suppressing:

- Point Lights;
- Spot Lights;
- IBL;
- emissive;
- unrelated illumination terms.

Reuse approved Directional shadow/cascade infrastructure where correct.

## D.3 Point Light

Assess and establish:

- position;
- color;
- intensity;
- range;
- distance attenuation/falloff;
- enabled state;
- renderer/GPU representation;
- revision/update behavior;
- Point Shadow participation.

Point Light must enter the same shared Direct PBR model used by Directional and Spot Lights.

Use one documented source of truth for Point attenuation.

Avoid duplicated or contradictory attenuation formulas.

Point Shadow must act as per-light visibility for that Point Light's Direct PBR contribution.

Reuse approved Point Shadow infrastructure where correct.

## D.4 Spot Light

Assess and establish:

- position;
- direction;
- color;
- intensity;
- range;
- inner cone;
- outer cone;
- cone falloff;
- enabled state;
- renderer/GPU representation;
- revision/update behavior.

Spot Light contribution must conceptually combine:

distance attenuation  
× cone attenuation  
× per-light shadow visibility  
× shared Direct PBR evaluation.

The visible Spot cone and Spot Shadow projection must use compatible semantics.

Do not allow unrelated definitions of Spot cone parameters between:

- ECS/component state;
- RenderFrame;
- shader lighting;
- shadow projection.

## D.5 Required Directional / Point / Spot Shadow Coverage

Directional, Point, and Spot Shadow capability is required for the corresponding supported Light type.

Required capability does not require replacement of an already correct approved implementation.

Audit and preserve existing correct shadow paths before proposing changes.

### Directional Shadow

Assess and validate:

- directional-light projection semantics;
- cascade behavior where applicable;
- cascade count;
- cascade split/distribution policy;
- caster relevance;
- receiver behavior;
- resource ownership;
- invalidation;
- Light/Camera change behavior;
- bias;
- filtering;
- cascade seams;
- cascade transition stability;
- resource lifetime;
- pass scheduling;
- Direct PBR integration.

Reuse approved Directional Shadow infrastructure where correct.

### Point Shadow

Assess and validate:

- omnidirectional shadow representation;
- face/layer projection semantics;
- Point Light range relationship;
- caster relevance;
- resource ownership;
- invalidation;
- Light movement;
- range changes;
- bias;
- filtering;
- cube-face/layer seams where applicable;
- resource lifetime;
- pass scheduling;
- Direct PBR integration.

Reuse approved Point Shadow infrastructure where correct.

### Spot Shadow

Audit whether the final Rendering baseline already implements a correct Spot Shadow path.

If missing or incomplete, plan bounded implementation using the approved shadow architecture.

Assess:

- Spot Shadow view/projection;
- frustum relationship to Spot cone;
- shadow-map description;
- resource ownership;
- resolution/quality;
- caster relevance;
- dirtiness/invalidation;
- Light movement;
- Light rotation;
- cone-angle changes;
- shadow enable/disable;
- bias;
- filtering;
- sampler behavior;
- RenderFrame identity;
- resource lifetime;
- pass scheduling.

The Spot illumination cone and Spot Shadow projection must use compatible semantics.

Spot Shadow must contribute only to the corresponding Spot Light Direct PBR term.

Do not create a second independent shadow subsystem if the approved Rendering shadow infrastructure can be extended safely.

## D.6 Common Shadow/PBR integration

Treat Shadow evaluation as per-light visibility in Direct PBR.

Conceptually:

DirectLightContribution  
= SharedPBRBRDF  
× LightRadiance  
× geometric terms  
× LightSpecificAttenuation  
× ShadowVisibility.

The exact formulation follows the selected PBR implementation.

Shadow terms must not incorrectly suppress:

- unrelated Lights;
- IBL;
- emissive;
- sky/environment contribution.

Assess common semantics for:

- bias;
- normal/slope bias where applicable;
- filtering;
- shadow enable state;
- caster visibility;
- resource lifetime;
- invalidation/revision;
- retained frame/resource behavior.

Do not reopen approved shadow architecture without:

- a demonstrated defect;
- a missing required integration;
- or separately approved architecture work.

## D.7 GPU Light representation

Establish coherent GPU representation for:

- Directional Light;
- Point Light;
- Spot Light.

Assess:

- typed engine Light data;
- packed representation;
- stable layout;
- Light type discrimination;
- capacity;
- deterministic identity/order;
- revision-driven uploads;
- binding ownership;
- replacement/lifetime.

Do not introduce a second contradictory semantic Light state solely for GPU upload.

## D.8 IBL / Indirect PBR

IBL is the indirect-lighting component of the PBR model.

Assess and establish:

- environment representation;
- orientation;
- intensity;
- diffuse irradiance;
- prefiltered specular environment;
- BRDF integration LUT;
- roughness/metallic interaction;
- resource ownership;
- replacement/publication;
- revision behavior.

Final PBR lighting should conceptually combine:

Direct PBR  
+ Indirect PBR / IBL.

Do not use a fixed arbitrary ambient constant as the final substitute for a complete IBL contract unless explicitly accepted as a limitation.

## D.9 Linear color workflow

Define one coherent color-space contract.

Audit:

- BaseColor texture interpretation;
- metallic/roughness/normal texture interpretation;
- authored Material colors;
- environment textures;
- emissive values;
- intermediate targets;
- HDR Scene Color;
- Bloom;
- tone mapping;
- final display conversion.

Explicitly distinguish:

- encoded/sRGB inputs;
- linear non-color data;
- linear HDR lighting data;
- final display/sRGB output.

Prevent:

- double sRGB decode;
- missing decode;
- double gamma correction;
- missing final display conversion.

## D.10 HDR Scene Color

Lighting must produce a documented linear HDR Scene Color before final display transformation.

Assess:

- HDR target format;
- precision;
- ownership;
- lifetime;
- resize behavior;
- MSAA/resolve interaction where relevant;
- pass integration;
- post-processing input.

Direct PBR, IBL, emissive contribution, and other HDR scene terms must retain meaningful values above normal display range before tone mapping.

## D.11 Required Physically Based Bloom

Physically Based Bloom is a required Milestone D capability.

Bloom consumes the linear HDR lighting result and belongs before final display conversion.

Bloom must NOT be embedded inside:

- PBR BRDF;
- Directional Light calculation;
- Point Light calculation;
- Spot Light calculation.

Conceptually:

Direct PBR  
+ IBL  
+ Emissive  
+ other HDR scene contribution  
→ Linear HDR Scene Color  
→ Bloom filtering/reconstruction/composite  
→ Exposure/pre-exposure according to the selected pipeline  
→ Tone Mapping  
→ Display.

Audit existing Bloom first.

If Bloom is absent or unsuitable, plan a bounded modern HDR Bloom implementation.

Evaluate:

- an energy-aware multi-resolution approach;
- or a technically equivalent implementation appropriate to the current renderer.

Assess:

- HDR input;
- downsample chain;
- filtering;
- upsampling/reconstruction;
- Bloom strength;
- radius/spread where supported;
- optional threshold/knee semantics where justified;
- transient resource ownership;
- resize behavior;
- compositing order;
- exposure interaction;
- tone-mapping interaction;
- GPU cost.

Do not implement Bloom as an arbitrary display-space blur disconnected from HDR energy.

Do not perform Lighting or Bloom in final sRGB/display space.

If threshold/knee controls exist, distinguish:

- artistic controls;
- HDR-energy semantics.

Bloom should support bright:

- emissive materials;
- direct-light highlights;
- specular highlights;

without requiring those sources to become fake Lights.

## D.12 Exposure

Establish at least a stable manual-exposure contract.

Assess:

- representation;
- ownership;
- defaults;
- Camera/view relationship where justified;
- application point;
- interaction with Bloom;
- interaction with tone mapping.

Automatic exposure is not required unless separately justified.

Exposure must not be hidden inside unrelated Material or Light state.

## D.13 Tone mapping and final display

Establish one defined HDR-to-display path.

Assess:

- selected tone mapper;
- exposure/pre-exposure ordering;
- Bloom compositing ordering;
- HDR input;
- display transform;
- final sRGB behavior;
- maintained-application consistency.

Do not allow different maintained applications to silently implement incompatible tone-mapping paths without explicit ownership.

## D.14 Lighting and Shadow visual-quality requirements

Functional correctness alone is insufficient.

Milestone D must establish an explicit visual-quality baseline for:

- Direct PBR response;
- Directional Light;
- Point Light;
- Spot Light;
- Directional Shadow;
- required Point Shadow;
- required Spot Shadow;
- normal mapping;
- IBL;
- HDR highlights;
- Bloom;
- exposure;
- tone mapping.

Shadow visual-quality review must inspect:

- contact-shadow quality;
- sharpness/softness;
- aliasing;
- shimmering;
- temporal instability;
- acne;
- peter-panning;
- light leaking;
- detached/floating shadows;
- cascade seams;
- cascade transition stability;
- Point-shadow face seams where applicable;
- Spot cone/frustum alignment;
- thin geometry;
- grazing-angle surfaces;
- near/far receivers;
- moving casters;
- moving Lights;
- moving Camera.

Do not approve a shadow path merely because it produces a visible shadow.

## D.15 Lighting/Shadow quality policy

Define explicit supported quality settings rather than scattered magic numbers.

Where applicable the quality policy should cover:

- shadow-map resolution;
- Directional cascade count;
- cascade distribution/splits;
- filter kernel/sample count;
- bias;
- slope/normal bias;
- Point-shadow resolution;
- Spot-shadow resolution;
- update/reuse policy;
- IBL quality;
- Bloom resolution chain/quality.

Defaults must be justified through both:

- visual reference quality;
- measured runtime cost.

Do not silently lower visual quality to satisfy performance targets.

Do not raise resolution/sample counts merely to improve screenshots without measuring cost.

## D.16 Lighting and Shadow performance/scalability

Measure Lighting and Shadow cost separately from unrelated Physics/application work.

Where supported record:

- frame CPU cost;
- frame GPU cost;
- per-Light CPU preparation;
- per-Light GPU cost;
- shadow-pass CPU cost;
- shadow-pass GPU cost;
- caster submissions;
- draw count;
- instanced draws;
- state changes;
- uploads;
- shadow resource memory;
- cache/reuse behavior;
- invalidation frequency;
- IBL cost;
- HDR target/resolve cost;
- Bloom/post-process cost;
- tone-map cost.

Measure under matched:

- resolution;
- hardware;
- GPU/driver;
- quality settings;
- pacing;
- warm-up;
- sample count;
- workload.

Report:

- typical behavior;
- tail behavior where timing infrastructure supports it.

Do not rely only on median where:

- p95;
- p99;
- known tail anomalies;

are relevant.

Explicitly measure representative scaling for:

- caster count;
- shadow-casting Light count;
- Directional cascade count;
- Point-shadow count;
- Spot-shadow count;
- shadow resolution;
- filter/sample count;
- dirty versus cached/reused shadows;
- Light count.

Do not assume fewer draw calls automatically mean better GPU tail latency.

Preserve and investigate known post-Rendering shadow-tail findings using matched evidence rather than speculative attribution.

Do not assume as required PRE_EDITOR features:

- Forward+;
- Clustered Lighting;
- tiled lighting;
- GI;
- DDGI;
- volumetric lighting;
- ray tracing;
- Area Lights.

Area Lights and Area-Light Shadows are intentionally deferred from the PRE_EDITOR baseline.

Do not introduce them as part of Lighting cleanup unless separately authorized.

If many-Light performance becomes a demonstrated bottleneck, propose the smallest evidence-driven future phase.

## D.17 Quality/performance tradeoff and default profile

For each supported Lighting/Shadow quality profile record:

- visible quality benefit;
- CPU cost;
- GPU cost;
- memory cost;
- intended workload.

Prefer the lowest-cost profile that satisfies accepted reference quality.

Do not:

- silently reduce image quality;
- disable required shadows;
- remove required caster coverage;
- introduce obvious artifacts;
- weaken correctness validation;

solely to improve benchmark numbers.

Milestone D must select and validate a practical default Lighting/Shadow configuration for `RigidBodySimulation`.

The audit proposes the quality/performance acceptance contract required by Section 18.2.

Do not invent numeric budgets.

Do not treat descriptive words such as:

- “acceptable”;
- “bounded”;
- “fast enough”;
- “stable enough”;

as measurable pass conditions without an approved criterion.

The default must provide:

- stable Directional/Point/Spot lighting;
- stable required Directional/Point/Spot shadow response;
- acceptable contact-shadow quality;
- correct PBR behavior;
- acceptable IBL;
- acceptable HDR/Bloom behavior;
- bounded CPU/GPU cost under the approved acceptance contract.

Higher-cost settings may remain optional.

## D.18 Reference validation

Provide reproducible visual/reference scenes covering at minimum:

- dielectric low roughness;
- dielectric high roughness;
- metallic low roughness;
- metallic high roughness;
- glossy tiled floor;
- Directional Light;
- Point Light;
- Spot Light;
- Directional Shadow;
- required Point Shadow;
- required Spot Shadow;
- no-shadow comparisons;
- normal mapping;
- thin shadow caster;
- large ground receiver;
- cascade transition;
- Point-shadow seam case;
- Spot cone/shadow alignment;
- moving Light / moving Camera temporal case;
- IBL;
- emissive HDR source;
- bright specular highlight;
- Physically Based Bloom enabled/disabled;
- exposure changes;
- tone mapping;
- dark scene;
- bright scene.

Record:

- exact scene identity;
- Camera pose;
- viewport/resolution;
- Light configuration;
- Material configuration;
- environment;
- shadow configuration;
- quality profile;
- HDR format/settings;
- Bloom settings;
- exposure;
- tone mapper;
- comparison criteria.

Human visual review is required for artifacts automated image comparison cannot reliably judge.

## D.19 Milestone D exit condition

Milestone D is complete only when:

- Directional/Point/Spot semantics are coherent;
- all three use the shared Direct PBR Material model;
- Point and Spot attenuation/falloff are defined;
- Directional/Point/Spot Shadows integrate as correct per-Light visibility;
- all required Directional, Point, and Spot Light Shadows are implemented or preserved and validated;
- GPU Light representation is stable;
- IBL/Indirect PBR is established;
- linear color responsibilities are explicit;
- HDR Scene Color is established;
- Physically Based Bloom is established;
- exposure is established;
- tone mapping/final display is established;
- reference scenes pass;
- visual-quality review passes;
- dynamic Shadow temporal stability meets the approved acceptance contract;
- explicit Lighting/Shadow quality settings exist;
- default quality settings are justified;
- required CPU/GPU performance evidence exists;
- relevant tail-latency findings are resolved or explicitly dispositioned;
- unresolved Lighting findings have explicit owner/gate/acceptance.

Area Light remains intentionally deferred and is not a Milestone D exit requirement.

The resulting Lighting/PBR/HDR contracts must be stable enough for Milestone E to persist and expose through shared:

- property;
- serialization;
- transaction;

mechanisms.

---

# 13. Milestone E — Engine Authoring Foundation

Milestone E establishes the persistent and authoring-facing Engine model consumed by Core Editor.

## E.1 Scene / ECS Authoring API

Explicitly audit:

- Scene creation;
- Entity creation;
- Entity deletion;
- duplication;
- component add/remove/get;
- parenting;
- unparenting;
- reparenting;
- queries;
- Scene clone;
- Scene replacement.

Core Editor should not require broad direct dependence on EnTT internals/raw registry operations.

EnTT may remain an implementation detail where appropriate.

Define Transform semantics:

- local Transform;
- world Transform;
- parent relationship;
- preserve-world reparent;
- preserve-local reparent;
- scale;
- negative/zero-scale policy;
- hierarchy revision.

## E.2 Project / Workspace Foundation

Add an explicit Project concept.

Assess:

- Project identity;
- project root;
- asset roots;
- Scene roots;
- derived/cache location;
- project settings;
- default/startup Scene;
- project-relative path/URI policy.

Distinguish:

Project settings  
≠ Editor/user preferences.

Do not persist absolute development-machine paths where project-relative identity is required.

## E.3 Persistent identity

Define:

- AssetId;
- EntityGuid;
- SceneId/SceneGuid;
- reference restoration;
- duplication semantics.

Persistent identity remains distinct from runtime handles.

## E.4 Serialization

Establish:

- Scene serialization;
- Entity/component/property serialization;
- hierarchy;
- asset references;
- Camera;
- Lighting;
- Physics components;
- Physics constraints;
- schema/version policy;
- unknown/missing data behavior;
- typed errors;
- semantic round-trip tests.

## E.5 AssetDatabase

Own:

- persistent AssetId;
- source path;
- asset type;
- import settings;
- dependencies;
- import state;
- revision;
- source/derived relationship.

AssetDatabase is not a GPU registry.

## E.6 Import/Reimport

Define:

- source detection;
- import/reimport;
- dependency changes;
- identity preservation;
- failure rollback;
- last-valid retention;
- safe publication;
- notifications.

## E.7 Property Metadata

Create one typed metadata foundation reusable by:

- serialization;
- transactions;
- Undo/Redo;
- Copy/Paste;
- later Inspector;
- future prefab/override systems.

Reuse common schemas for:

- Material;
- Geometry;
- Camera;
- Lighting;
- Physics;
- Scene components.

Do not create separate reflection systems per subsystem.

## E.8 Mutation Transactions

Authoritative mutation must pass a defined transaction boundary where Undo/Redo applies.

Cover:

- Transform;
- material;
- Camera;
- Light;
- Physics properties;
- Entity create/delete/duplicate/reparent;
- grouped changes.

Define:

- validate;
- apply;
- commit;
- rollback;
- revision;
- notification timing.

Events notify committed state changes.

Events do not perform authoritative mutations.

## E.9 Undo/Redo

Plan engine-level semantics for:

- property restoration;
- Entity create/delete;
- hierarchy;
- Material;
- Camera;
- Light;
- Physics/constraint property edits.

No Editor UI here.

## E.10 Dirty/revision

Define:

- Scene dirty;
- Asset dirty;
- document revision;
- saved revision.

## E.11 Scene/World lifecycle

Define:

- create;
- load;
- unload;
- clone;
- replace;
- destroy.

## E.12 Edit/Play isolation

Define an explicit model preventing runtime simulation from corrupting persistent authoring state.

Cover:

- Entities;
- Transforms;
- Physics state;
- Materials;
- Camera/Light;
- mutable assets;
- subscriptions;
- queued callbacks/events;
- runtime resource ownership.

## E.13 Authoring notifications

Using Milestone B foundations, define useful notifications such as:

- Scene lifecycle;
- Entity lifecycle;
- reparent;
- property committed;
- Asset imported;
- Asset reimported;
- Asset failed;
- resource replacement.

Exact names must come from source-based design.

## E.14 Structured Authoring Diagnostics

Audit whether one lightweight structured diagnostic contract is needed for:

- Shader errors;
- Material errors;
- Mesh import;
- Asset import/reimport;
- serialization;
- Event/Input;
- Physics;
- Lighting.

Prefer structured fields such as:

- severity;
- subsystem;
- code;
- message;
- AssetId/EntityGuid when relevant;
- source path/location when relevant.

Do not create a giant singleton DiagnosticsManager without evidence.

## E.15 Authoring task state

Assess observable long-running task state for future Editor consumers:

- Pending;
- Running;
- Succeeded;
- Failed;
- Cancelled.

Relevant operations may include:

- import;
- reimport;
- shader compilation;
- asset processing;
- thumbnail generation where later justified.

Do not create a competing general job system.

## E.16 Debug / Authoring Visualization

Establish semantic Engine APIs for:

- line;
- ray;
- box;
- sphere;
- frustum;
- grid;
- axis;
- icon/billboard;
- bounds;
- Camera;
- Light;
- collider;
- Physics contact/constraint;
- normals/tangents where justified.

Core Editor must not issue raw OpenGL for these visualizations.

## E.17 Multi-object foundations

Assess Engine-level requirements for:

- grouped mutations;
- parent-child selected edits;
- local/world conventions;
- pivot semantics;
- avoiding double Transform application.

Do not implement:

- selection UI;
- gizmo UI;
- Inspector UI;
- mixed-value UI;
- Content Browser UI.

---

# 14. Milestone F — Final Integration and Core Editor Handoff

Milestone F proves the Engine can support Core Editor without bypassing Engine contracts.

`RigidBodySimulation` is the sole application for new end-to-end integration execution and acceptance, as defined in Section 1.2.

Integrate relevant capabilities during A-E.

Do not defer all consumer adoption until F.

## A evidence

`RigidBodySimulation` must demonstrate:

- simple Geometry Templates;
- standard Material Templates;
- sharing;
- duplication;
- MakeUnique;
- resize/stretch;
- simplified manager/resource API.

## B evidence

Demonstrate:

- input routing;
- focus;
- capture;
- consumption;
- Camera control;
- Editor Camera;
- Scene Camera;
- picking.

## C evidence

Demonstrate:

- sphere lattice settling;
- box stacking;
- Physics API;
- Physics↔Rendering Transform flow;
- constraints;
- chain;
- hinge/limits;
- Ragdoll drop/settling;
- Physics debug visualization;
- Physics performance.

## D evidence

Demonstrate:

- Directional Light integrated with shared Direct PBR;
- Point Light integrated with shared Direct PBR;
- Spot Light integrated with shared Direct PBR;
- required Directional Shadow as per-Light Direct PBR visibility;
- required Point Shadow as per-Light Direct PBR visibility;
- required Spot Light Shadow as per-Light Direct PBR visibility;
- Point attenuation;
- Spot cone/falloff;
- Standard PBR Material response;
- IBL / Indirect PBR;
- linear color workflow;
- HDR Scene Color;
- emissive HDR response;
- Physically Based Bloom;
- exposure;
- tone mapping;
- final display/sRGB behavior;
- Light/Shadow visual-quality reference scenes;
- dynamic-Shadow temporal-stability review;
- explicit default/quality settings;
- matched Lighting/Shadow CPU/GPU measurements;
- Shadow resource memory where measurable;
- dirty/cached Shadow behavior;
- relevant p95/p99 tail behavior;
- Bloom visual quality versus GPU cost;
- final human visual acceptance in `RigidBodySimulation`.

At least one controlled `RigidBodySimulation` reference configuration must demonstrate:

Standard PBR Material  
→ Directional / Point / Spot Direct Lighting  
→ Per-Light Shadow Visibility  
→ IBL  
→ Emissive/HDR Scene Contribution  
→ Linear HDR Scene Color  
→ Physically Based Bloom  
→ Exposure / pre-exposure according to the selected pipeline  
→ Tone Mapping  
→ Final Display.

Lighting/Shadow acceptance requires all four dimensions:

1. correctness;
2. visual quality;
3. temporal stability;
4. performance.

## E evidence

Demonstrate:

- Scene/ECS authoring API;
- persistent IDs;
- save/unload/load;
- Project/AssetDatabase;
- reimport;
- property transactions;
- Undo/Redo;
- Edit/Play isolation;
- structured diagnostics where applicable.

## F integrated scenario

At minimum validate a flow equivalent to:

Create/open Project  
→ create Scene  
→ create primitive geometry  
→ create standard material  
→ assign material/mesh  
→ move/rotate/scale/stretch  
→ duplicate/share/MakeUnique  
→ create Camera  
→ use Editor Camera  
→ create Directional Light  
→ create Point Light  
→ create Spot Light  
→ enable/configure required Directional/Point/Spot Shadows  
→ verify Directional Shadow  
→ verify Point Shadow  
→ verify Spot Light Shadow  
→ verify Shadow visual quality and temporal stability  
→ configure IBL/environment  
→ configure emissive HDR reference object  
→ verify Linear HDR Scene Color  
→ verify Physically Based Bloom  
→ configure exposure  
→ verify Tone Mapping/final display  
→ create Physics bodies/colliders  
→ create representative constraint/Ragdoll scenario  
→ render/pick/interact  
→ modify property transactionally  
→ Undo  
→ Redo  
→ save  
→ unload  
→ reload  
→ verify persistent references  
→ enter Play  
→ simulate Physics  
→ Stop  
→ verify authoring state  
→ import/reimport asset  
→ verify safe resource replacement  
→ verify notifications  
→ verify reference images  
→ verify Lighting/Shadow performance  
→ verify Physics performance  
→ verify shutdown.

Final gates:

- API simplicity;
- manager/resource dependency direction;
- Code Quality;
- Rendering regression;
- Physics correctness/stability/performance;
- Lighting/PBR/HDR correctness;
- Light/Shadow visual quality;
- Light/Shadow temporal stability;
- Light/Shadow performance;
- required Directional, Point, and Spot Light Shadows;
- Physically Based Bloom;
- persistence correctness;
- Edit/Play isolation;
- Debug/Release;
- `RigidBodySimulation` human acceptance.

Core Editor must not need normal direct knowledge of:

- OpenGL;
- GLAD;
- SDL implementation types;
- Assimp internals;
- GpuMesh;
- FrameResource;
- raw Shader program IDs;
- internal publication;
- raw EnTT registry operations for normal authoring;
- Physics solver internals.

---

# 15. Canonical findings and classifications

Each actionable finding records:

- stable ID;
- inspected baseline;
- evidence;
- impact;
- technical status;
- severity;
- primary owner;
- affected gate;
- dependencies;
- work item;
- required verification.

Primary categories:

- `RENDERING_COMPLETION_DEFECT`;
- `PRE_A_CODE_QUALITY`;
- `PRE_A_PERFORMANCE_REMEDIATION`;
- `MILESTONE_A`;
- `MILESTONE_B`;
- `MILESTONE_C_PHYSICS`;
- `MILESTONE_D_LIGHTING`;
- `MILESTONE_E_AUTHORING`;
- `MILESTONE_F_HANDOFF`;
- `CORE_EDITOR`;
- `INTENTIONALLY_DEFERRED`.

Physics subcategories may additionally identify:

- correctness;
- stability;
- performance;
- API;
- constraint;
- ragdoll;
- authoring gap.

Lighting subcategories may additionally identify:

- Direct PBR;
- Directional Light;
- Point Light;
- Spot Light;
- Directional Shadow;
- Point Shadow;
- Spot Shadow;
- shadow integration;
- visual quality;
- temporal stability;
- shadow performance;
- IBL;
- color workflow;
- HDR;
- Bloom;
- exposure;
- tone mapping;
- authoring gap.

Gate labels may include:

- `BLOCKS_PRE_EDITOR_ENTRY`;
- `BLOCKS_MILESTONE_A_START`;
- `BLOCKS_MILESTONE_EXIT:<name>`;
- `BLOCKS_CORE_EDITOR_HANDOFF`;
- `NONBLOCKING_FOLLOWUP`.

Priority remains separate:

- `MUST_FIX_BEFORE_A`;
- `REQUIRED_BY_OWNER_PHASE`;
- `FIX_WHEN_TOUCHED`;
- `DEFER`.

Accepted deferral is not technical resolution.

---

# 16. PRE_EDITOR governance

Use one numeric PRE_EDITOR phase series:

PRE_EDITOR Phase 00  
PRE_EDITOR Phase 01  
PRE_EDITOR Phase 02  
...

Do not create separate workflow identities such as:

- A01;
- Physics01;
- Lighting01;
- Event01.

A-F are roadmap milestones only.

The proposed scope preserves mandatory:

- Directional/Point/Spot Lighting;
- Directional/Point/Spot Shadows;
- IBL;
- HDR;
- Physically Based Bloom;
- Physics milestone;
- all five Rendering performance residual families.

Do not silently drop these requirements to shorten the roadmap.

Prefer one canonical namespace:

`docs/pre_editor/`

unless a proven established equivalent already exists.

Reuse successful workflow concepts:

- START;
- REVISE;
- APPROVE;
- REJECT;
- scope amendments;
- manifests;
- seals;
- snapshots;
- evidence reuse;
- protected paths;
- Git/publication safety.

Do not create parallel governance frameworks without demonstrated need.

Storing this master instruction or audit proposals under `docs/pre_editor/` is documentation only.

It does not:

- activate command routing;
- create approval state;
- start PRE_EDITOR Phase 00.

All new PRE_EDITOR state remains:

`PROPOSED / NOT_STARTED`

until separately authorized.

## 16.1 PRE_EDITOR branch and worktree activation plan

Complete the current audit and planning in the inspected Rendering worktree.

Creating or switching to a PRE_EDITOR branch/worktree belongs to later separately authorized activation/minimal setup, not this audit.

Read-only inspection of an existing Physics worktree for provenance remains within the audit scope.

The inactive activation/minimal-setup proposal must specify:

- owner review and acceptance of the PRE_EDITOR plan before activation;
- separate explicit authorization for PRE_EDITOR activation/minimal setup;
- a dedicated PRE_EDITOR branch and worktree using established repository conventions;
- the full verified Phase 68 commit SHA as the starting point, or the full SHA of a separately approved corrective entry baseline under Section 3;
- the proposed branch name and worktree path;
- independent build directories, intermediate objects, binaries, libraries, writable runtime settings/caches, and validation outputs;
- how the master instruction, audit findings, roadmap, proposed contracts, and compact manifests will be brought into the new branch;
- the applicable setup, commit, and publication bookkeeping under the authorized workflow.

Do not select a starting point merely because it is the current HEAD.

Preserve the approved Rendering historical reference and all protected or unrelated work.

No PRE_EDITOR build, application run, cache, capture, or evidence writer may target Phase 68 sealed output locations.

Share immutable tools or inputs only where doing so preserves that isolation; mutable outputs must remain separate.

Document transfer must preserve provenance and include only the intended audit/proposal files.

Do not use it to carry unrelated local edits or experimental Physics changes into PRE_EDITOR.

During this audit, record this transfer method as a proposal only; the existing no-stage/no-commit/no-push boundary remains in effect.

Creating a worktree does not itself approve an implementation phase or require repeating the historical application/test matrix.

Apply Sections 1.2 and 18.2 to later validation.

---

# 17. Architecture invariants

Carry forward all approved Rendering invariants and propose at minimum:

- normal Authoring/Core Editor code uses GEngine semantic contracts;
- OpenGL/GLAD/SDL/Assimp implementation details remain private;
- GPU objects are not authoring identities;
- persistent IDs differ from runtime handles;
- manager/service responsibilities are explicit;
- normal callers do not depend on manager chains;
- FrameResource remains renderer-owned unless a semantic public role is demonstrated;
- ShapeManager does not remain a competing geometry architecture;
- ShaderManager does not remain an unbounded god service;
- Geometry Templates reuse MeshAsset/MeshHandle;
- Material Templates reuse Shader/Pipeline/Material architecture;
- Scene/ECS public authoring does not require raw registry internals;
- Project owns project-relative content roots/settings semantics;
- Transform/object dimensions differ from topology modification;
- Scene Camera and Editor Camera differ in role;
- event lifetime/thread/routing semantics are explicit;
- EventBus does not replace explicit scheduling;
- Physics stepping remains explicit;
- Physics/Scene/Rendering Transform ownership is explicit;
- Constraints reuse the same Physics solver/body architecture;
- Ragdoll composes normal Physics primitives;
- Directional, Point, and Spot Lights use one coherent shared Direct PBR Material/BRDF model;
- Light types provide type-specific direction, radiance, attenuation, cone behavior, and visibility rather than independent duplicated Material models;
- Directional, Point, and Spot Shadows act as per-Light visibility terms for Direct PBR;
- Shadow visibility must not incorrectly suppress unrelated Lights, IBL, emissive contribution, or other non-shadowed terms;
- IBL is the indirect-lighting component of the PBR pipeline and shares Material metallic/roughness semantics with Direct PBR;
- Lighting and Shadow quality settings are explicit, reproducible, and performance-measured;
- Lighting/Shadow acceptance includes correctness, visual quality, temporal stability, and performance;
- Physically Based Bloom is part of the linear-HDR post-lighting/display pipeline, not the BRDF;
- Bloom consumes linear HDR scene energy before final display conversion according to a defined exposure/pre-exposure and tone-mapping contract;
- Area Lights and Area-Light Shadows are intentionally deferred from the PRE_EDITOR baseline unless separately authorized;
- Lighting/PBR/HDR semantics are stable before persistent metadata freezes;
- mutations and notifications are distinct;
- Undoable edits use transactions;
- AssetDatabase is not a GPU registry;
- common property schemas are shared;
- convenience APIs do not hide repeated expensive hot-path work;
- Rendering and Physics runtime remain independent from Core Editor UI types.

---

# 18. Bounded phase planning

Plan executable phases in this order:

1. minimal PRE_EDITOR setup;
2. Code Quality policy/adoption;
3. evidence-required pre-A remediation;
4. Milestone A;
5. Milestone B;
6. Milestone C Physics;
7. Milestone D Lighting/PBR/HDR;
8. Milestone E Authoring;
9. Milestone F integration/handoff.

Use actual findings to decide phase count.

Do not force arbitrary phase numbers.

Do not create placeholder phases.

Separate:

- mechanical formatting;
- semantic cleanup;
- API migration;
- correctness fixes;
- feature implementation;
- performance optimization;

when combining them would impair reviewability.

For each proposed phase record:

- goal;
- linked findings;
- dependencies;
- entry conditions;
- approved predecessor;
- bounded scope;
- verified paths/symbols where known;
- justified change budget;
- explicit non-goals;
- preserved invariants;
- required validation;
- evidence reuse;
- observable exit;
- `RigidBodySimulation` coverage;
- manual acceptance where useful;
- review/seal/approval behavior.

For Lighting/Shadow phases additionally specify when applicable:

- reference visual workload;
- quality profile;
- CPU/GPU measurements;
- memory measurements;
- temporal-stability checks;
- tail-latency checks;
- expected human visual acceptance.

Later phase implementation detail may be refined before START, but milestone dependency/ownership must already be coherent.

## 18.1 Shared prerequisites and milestone ordering

Preserve the high-level order:

setup  
→ Code Quality  
→ evidence-required pre-A remediation  
→ A  
→ B  
→ C  
→ D  
→ E  
→ F.

Inspect existing shared foundations before proposing new ones.

Record a dependency table containing:

- earliest consumer;
- current provider;
- evidence;
- missing minimum contract if any;
- proposed providing phase;
- later extension owner.

At minimum resolve:

| Shared foundation | Earliest relevant consumer | Later extension |
|---|---|---|
| Runtime Scene/Entity operations and sharing semantics | A geometry/material assignment and duplication | E authoring API, persistent identity, serialization, and transactions |
| Typed material/property semantics | A standard material APIs | D coherent PBR/HDR behavior; E metadata and persistence |
| Authoritative mutation and successful-change notifications | B Event/Input | E transactions, rollback, Undo/Redo, and commit-time notifications |
| Runtime Transform ownership and hierarchy semantics | B Camera and C Physics integration | E complete authoring hierarchy, persistence, and Edit/Play isolation |
| Shared semantic debug rendering | C collider/contact/constraint visualization, or an earlier demonstrated consumer | E broader authoring visualization |
| Structured resource/authoring diagnostics where justified | Earliest affected A-D operation | E shared authoring diagnostics |

Use an existing approved provider when it already satisfies the contract.

When evidence proves a prerequisite is missing:

- assign only its smallest required shared foundation;
- assign it to a bounded phase at or before its earliest consumer.

Do not:

- move all of Milestone E forward;
- create a second subsystem;
- invent placeholder phases.

Early successful-change notifications may report authoritative API changes before full transactions exist.

They must not claim:

- rollback semantics;
- Undo semantics;

until E implements them.

E must subsequently align authoring notifications with successful transaction commit.

Early runtime identity and typed schemas must allow the later persistent-identity/metadata contract without pretending that:

- AssetDatabase;
- persistent serialization;

already exists.

A standard Material API may initially use the approved Rendering Material implementation.

D establishes the complete shared PBR/HDR behavior using that same Material model.

Do not create a second standard Material system.

Camera and Physics integration may use existing Scene/Transform contracts.

Missing minimum contracts belong before their first dependent phase.

Full persistent authoring remains E's responsibility.

For C debug visualization:

- reuse the approved Renderer;
- use a small semantic API.

E extends it instead of replacing it with another path.

Show that every phase dependency is satisfied by:

- the approved entry baseline;
- or an earlier proposed phase.

Do not leave an A-D exit condition dependent on completion of E.

An unresolved provider is a planning dependency gap, not a reason to silently implement future work during the audit.

## 18.2 Predeclared validation and acceptance contracts

During this audit, propose the smallest sufficient validation contract for each phase.

Do not execute the full future validation matrix.

Before a future implementation/performance phase starts, its approved contract must identify the applicable measurable gates and the evidence sufficient to decide them.

For relevant correctness, visual, temporal, or performance work, specify:

- exact `RigidBodySimulation` scenario or preset;
- source/build identity;
- input identity;
- comparison baseline;
- hardware;
- driver;
- configuration;
- viewport/resolution;
- Camera;
- Lights;
- Materials;
- Physics state;
- quality profile;
- pacing where relevant;
- affected behavior;
- reason each focused check is necessary;
- metrics;
- units;
- measurement boundaries;
- observer/profiling overhead assumptions;
- applicable absolute CPU/GPU budgets or allowed regressions;
- memory limits where relevant;
- required typical/tail metrics;
- repeatability qualification;
- sample sufficiency;
- warm-up;
- samples per run;
- number of runs;
- maximum repeats;
- total execution bound;
- declared stop conditions;
- visual reference conditions;
- permitted intentional differences;
- image-comparison criteria where useful;
- explicit human acceptance;
- dynamic sequences for Camera/Light/caster motion where relevant;
- contact-shadow checks;
- seam checks;
- temporal-stability checks;
- evidence locations;
- identity/provenance;
- reuse conditions;
- exact pass/fail/inconclusive rule.

Choose a small representative workload set justified by the affected behavior.

Do not automatically form a Cartesian product of every:

- Light count;
- resolution;
- cascade count;
- filter setting;
- Physics scenario;
- application.

Use matching historical evidence only when its:

- source;
- build;
- workload;
- settings;

make the comparison valid.

A historical absolute target with unresolved comparability remains an open finding.

Do not claim a fresh comparison closes it without evidence.

Numeric budgets and tolerances must come from:

- applicable approved criteria;
- justified baseline evidence;
- or a recorded owner decision.

If unknown, mark them `OPEN_DECISION` in the affected phase contract with the evidence needed to set them.

Do not invent a convenient number.

Unresolved acceptance criteria block:

- the affected phase START;
- or the affected gate;

not unrelated read-only audit work.

The audit may still be `READY_FOR_HUMAN_REVIEW` when:

- decisions are explicit;
- no evidence gap invalidates its conclusions.

Distinguish preservation of existing accepted visuals from intentional new:

- PBR;
- HDR;
- Shadow;
- Bloom;

behavior.

New behavior needs:

- a controlled proposed reference;
- owner visual acceptance.

Do not demand accidental pixel identity with a pipeline that did not previously provide that behavior.

Keep automated image comparison and human visual review complementary.

A screenshot or successful launch alone cannot pass:

- temporal stability;
- performance.

Insufficient samples, unstable repeated runs, or missing relevant tail evidence are:

`INCONCLUSIVE`

rather than passing.

Do not:

- selectively discard unfavorable runs;
- extend the campaign until it passes;
- adjust thresholds after seeing the result.

Any protocol change must be recorded and reviewed before the next comparison.

Retain failed and inconclusive evidence with:

- its baseline;
- its finding ID.

Reuse unaffected accepted evidence instead of broad retesting.

---

# 19. Readiness decisions

Report separately.

## Audit Completion

Exactly one:

- `COMPLETE_FOR_REVIEW`
- `INCOMPLETE`
- `BLOCKED`

Use `COMPLETE_FOR_REVIEW` only when:

- every required audit topic has a documented disposition;
- no evidence gap invalidates the reported conclusions.

## Plan Readiness

Use:

`READY_FOR_HUMAN_REVIEW`

only when Code Quality, remediation, and A-F all have coherent:

- ownership;
- dependency;
- scope;
- validation;
- routing.

This status does not authorize implementation.

## Next Action

Identify the smallest concrete owner decision required after plan review.

Do not invent a currently supported command or permission.

## Milestone A Readiness

Report exactly one:

- `NOT_READY`;
- `READY_PENDING_EXPLICIT_START`;
- `BLOCKED`.

Do not claim proposal-only work implemented prerequisites.

---

# 20. Final output and stop

Before completion:

- check internal consistency;
- check A-F ordering;
- check earliest-consumer shared prerequisites;
- check baseline identities;
- check Rendering performance residual traceability;
- check Physics baseline/provenance;
- check required chain, hinge/limits, and basic Ragdoll scope against C.9 and Section 14;
- check Manager/Resource findings;
- check Lighting/PBR/HDR ownership;
- check mandatory Directional, Point, and Spot Shadow requirements;
- check Bloom requirement;
- check Lighting/Shadow visual-quality requirements;
- check Lighting/Shadow performance requirements;
- check predeclared acceptance criteria;
- check Area Light remains deferred;
- check duplicate findings;
- check authority boundaries;
- check the inactive branch/worktree activation plan, output isolation, and audit-document transfer under Section 16.1;
- check isolated diagnostic outputs;
- check the sole-application/minimal-validation policy;
- verify index remains unchanged;
- verify protected/sealed files remain unchanged;
- list actual proposal/audit files created or modified.

Return a compact summary containing:

1. verified Phase 68 baseline/publication;
2. effective PRE_EDITOR entry baseline;
3. Physics authoritative/experimental baseline identities;
4. Audit Completion;
5. major findings by ID/gate;
6. Manager/Resource API findings;
7. Rendering performance residual disposition;
8. Physics correctness/stability/performance findings;
9. Lighting/PBR/HDR findings;
10. Light/Shadow visual-quality and temporal-stability findings;
11. Lighting/Shadow performance and tail-latency findings;
12. **shared-prerequisite/dependency findings, including any minimum foundations that must move before their earliest A-D consumer;**
13. proposed PRE_EDITOR phase/dependency index covering Code Quality + A-F;
14. Plan Readiness;
15. Next Action;
16. Milestone A Readiness;
17. proposal file locations;
18. smallest next owner decision.

STOP.

Do NOT:

- activate PRE_EDITOR;
- implement fixes;
- implement A-F;
- create approval/receipt state;
- commit;
- tag;
- push;
- begin another phase automatically.

`READY_FOR_HUMAN_REVIEW` applies only to the audit/proposal package.

It is not permission to execute PRE_EDITOR.