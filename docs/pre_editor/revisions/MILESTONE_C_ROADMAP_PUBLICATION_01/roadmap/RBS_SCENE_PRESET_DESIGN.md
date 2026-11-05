# Extend the existing RBS scene presets

**ACCEPTED DESIGN; NOT IMPLEMENTED.** This design belongs to the seven-phase Milestone C amendment. It proposes a practical startup selector in the maintained `RigidBodySimulation` application. It creates no new application, launch authority, evidence campaign, live Scene-switch feature or production change.

## Audited starting point

The existing `RigidBodySimulation/include/RbsLaunchConfig.h` already defines `Rbs::RbsScenePreset` with `GeometryGallery`, `SphereDiamond`, `SphereLattice`, `BoxStack` and `SphereBoxStack`. `LaunchConfig.scene` defaults to `GeometryGallery`; ordinary `RigidBodySimulation/src/main.cpp` explicitly selects `SphereBoxStack` through a `constexpr`. These are separate existing contracts. `RigidBodySimulation/tests/LaunchConfigChecks.h` currently checks the typed `GeometryGallery` default. A proposal to make ordinary startup configurable must not silently change either default.

`main.cpp` creates one immutable `LaunchConfig` that outlives the app and `EngineContext`. `RigidBodySimulationApp` borrows it as `const Rbs::LaunchConfig&`. Current `argc/args` handling validates the executable path for assets; it is not an ordinary scene-choice parser. An isolated Collider passive-observation compile path has its own admitted selection, and `RbsScene.cpp` contains a Phase 23 Debug-gallery override. Those diagnostic paths do not prove ordinary runtime selection exists and cannot silently override a user-selected new preset in qualified runs.

`RigidBodySimulation/src/RbsScene.cpp` creates the scene root, render resources, materials/lights, selected physics fixture, environment/ground/walls and then starts Physics. Geometry/material/shader/async demonstrations belong only to `GeometryGallery`. Existing startup passes can exit with typed errors. Scene runtime owns its Physics world and bodies; no public joint provider is currently implemented.

`RigidBodySimulation.cpp` teardown cancels input, releases subscriptions, clears app gallery state, shuts down audio, ensures the context is current, joins mesh work, retires the editor-view identity and releases scenes/frame submission/resources. `_Scene::OnPhysics3DStop` clears runtime pose callbacks/bindings before Physics exit. These are useful providers, but inspection of separate pieces is **not evidence that an arbitrary live scene replacement is safe**.

## One selector, one immutable startup snapshot

Extend `Rbs::RbsScenePreset` and the existing `LaunchConfig` rather than introducing a second scene-name enum or app. Keep ordinary no-argument startup explicitly `SphereBoxStack`; keep `LaunchConfig{}` as `GeometryGallery` for existing typed consumers unless a separately reviewed compatibility change becomes necessary.

The proposed initial interface is process startup arguments parsed once before app creation. Example spelling below is a design proposal, not a command currently supported by the executable:

```text
RigidBodySimulation.exe
RigidBodySimulation.exe --scene sphere-lattice
RigidBodySimulation.exe --scene distance-chain --chain-links 4
RigidBodySimulation.exe --scene distance-chain --chain-links 8
RigidBodySimulation.exe --scene distance-chain --chain-links 16
RigidBodySimulation.exe --scene ball-and-socket
RigidBodySimulation.exe --scene hinge-limits
RigidBodySimulation.exe --scene passive-ragdoll
RigidBodySimulation.exe --scene motors
RigidBodySimulation.exe --scene movers
RigidBodySimulation.exe --scene book-conformance --case distance-chain-8
```

After the implementing phase is built once, selecting an implemented preset requires **relaunch with different arguments, not source edits or rebuilds**. A compact `--help`/available-presets response can make names discoverable without launching the engine. Only implemented presets are advertised and accepted; future Phase 27–31 names are not executable placeholders in Phase 26. Existing names remain stable.

Parsing is small, explicit and typed. Unknown names, duplicate/conflicting selections, truncated arguments, nonnumeric values and unsupported combinations return a clear bootstrap error before world creation. Do not silently clamp chain lengths, fall back after invalid input, infer a scene from environment variables or reuse a historical diagnostic flag as the public selector. The app receives the resolved enum plus a validated preset-specific configuration. Accepted output/config-path options, if later needed, must obey the PRE_EDITOR output graph; no new arbitrary filesystem writer is part of ordinary scene selection.

Scene selection and validation instrumentation remain different inputs. Picking `sphere-lattice` does not run the frozen Phase 21 diagnostic campaign, enable a hidden profiler, change sleep/pass settings or imply stability acceptance. Diagnostic build overrides are displayed/recorded distinctly; an incompatible override is rejected rather than mislabeling the actual scene. Historical source-selected captures remain historical evidence with their actual configuration.

## Preset catalogue and ownership

Existing enum identities and scenario semantics are retained. New identifiers below are proposed additions when their owning phases are implemented.

| Existing/proposed enum | Startup token | Owning phase | Complete scene and configuration |
| --- | --- | --- | --- |
| `GeometryGallery` (existing) | `geometry-gallery` | Existing provider | Existing geometry/material/shader/async examples and their admitted validation behavior. Keep the current typed-config default. |
| `SphereDiamond` (existing) | `sphere-diamond` | Existing provider | Existing sphere/convex-diamond setup, impulses and environment. No incidental content change. |
| `SphereLattice` (existing) | `sphere-lattice` | Existing provider | Existing ordinary deterministic lattice. Historical frozen object-list reproduction remains its separately identified diagnostic input; names alone do not establish equivalence. |
| `BoxStack` (existing) | `box-stack` | Existing provider | Existing box-stack setup and environment. |
| `SphereBoxStack` (existing) | `sphere-box-stack` | Existing provider | Existing mixed stack/impact scene; **ordinary no-argument default**. Preserve accepted Collider/Transform selection behavior. |
| `DistanceChain` (proposed) | `distance-chain` | 26 | One static anchor plus exactly 4, 8 or 16 dynamic links, N distance joints for N links; one default admitted link count, proposed 8. Include only declared environment and debug helpers. |
| `BallAndSocket` (proposed) | `ball-and-socket` | 27 | Anchored free swing and loaded-pair variants under one typed configuration; translation lock and free rotation visible. |
| `HingeLimits` (proposed) | `hinge-limits` | 28 | Free hinge and declared lower/upper-limit variant; the exact minimal angular provider needed by the ragdoll is independently identified. |
| `PassiveRagdoll` (proposed) | `passive-ragdoll` | 29 | Frozen small body/bone graph with normal bodies/shapes/joints/limits and declared self-collision policy; drop/settle and one controlled wake. No motor hidden in this preset. |
| `Motors` (proposed) | `motors` | 30 | One bounded hinge angular-velocity drive with declared loaded/limit/disabled variants; visible target and effort limit. |
| `Movers` (proposed) | `movers` | 31 | One deterministic kinematic platform and declared dynamic contacts; start/stop/reverse with simulation-time motion. |
| `BookConformance` (proposed) | `book-conformance` | 32 | Required `--case` resolves before world creation to an existing admitted builder/configuration, linked to its chapter/engine-deviation ledger row. No combined-world implementation or automatic campaign. |

Phase 32's `BookConformance` entry makes the book-related scenes directly selectable while reusing the same builders. It requires an explicit admitted case; no omitted-case default launches a full sequence. For example, `distance-chain-8` resolves to `DistanceChain` with `linkCount = 8`. Other case entries refer to implemented ball/socket, hinge/limit, ragdoll, motor or mover configurations only as selected by the conformance matrix. Each entry names its reference/engine-adaptation row and exact underlying configuration. Existing core-contact demonstrations may be referenced with their actual unresolved status; a book case name never converts historical failures to PASS.

Record the requested `BookConformance` case separately from the resolved underlying scene/variant so observations cannot misidentify the workload. Resolve before resources/world creation; reject missing, unknown, incompatible or unimplemented cases. This is one existing enum/configuration path, not a second scene identity system, duplicated Physics builder or mutable live selector. Phase 32 proposes 2–4 RBS production members for enum/configuration/parser/dispatch plus 1–3 supporting check files and compact records; it includes no Physics changes. Exact internal identifier spelling can be frozen before implementation; published startup tokens should remain stable once accepted.

## Deterministic configuration, separate from acceptance

For each new preset, store one small typed configuration with a version and an explicit deterministic construction order. Prefer explicit object lists/formulas and fixed initial state over random generation. If a seed is meaningful, freeze both seed and generator/version: a seed alone is not an object-list identity. No result-dependent configuration changes are allowed.

| Configuration field | Required meaning |
| --- | --- |
| Preset/version and variant | Distinguishes ordinary fixture content from historical diagnostic fixtures and from another variant. |
| Units and coordinate convention | Length/mass/time units, radians, handedness, body-local anchors/axes, joint reference orientation. |
| Bodies and shapes | Ordered IDs, dimensions, poses, linear/angular velocities, masses/inertias, material and collision layer/mask. Explicit static root where used. |
| Joint topology | Ordered body pairs, type, local anchors/frames, distance or angular bounds, connected-pair collision policy. Logical bone mapping is separate from runtime body ownership. |
| Fixed-step policy | Existing default 1/60 s or explicitly admitted pre-start 1/120 s; unchanged scene backlog/max-steps policy; exact solver passes and sleep/material/contact settings. |
| Motion | Predetermined impulses or simulation-tick-indexed drive/mover commands; no render-delta, frame-rate or wall-clock stimulus. |
| Presentation | Deterministic initial camera/framing, normal controls, explicit debug visibility; declared helper/ground/wall membership. Preserve render resources and current Transform/picking behavior. |
| Validation-only protocol | Accepted workload/duration/error/energy/settling/cost limits and bounded run settings when a check needs them. These are supplied by the approved matrix/OD-08, not invented by the preset. |

For chain configurations define `linkCount` as the number of **dynamic link bodies**, not total bodies or joints. With a single static root and a root-to-first joint, `linkCount = N` means N + 1 chain bodies and N distance joints; unrelated declared ground/walls are counted separately. Support 4, 8 and 16; propose 8 as the ordinary chain default. Each variant freezes exact body dimensions, local anchors, rest distances, spacing, material and initial state at admission. The 4/8/16 set supplies a small informative scaling comparison, not license to vary every mass/pass/contact option as a Cartesian product.

Joint phases use these configurations to make workloads repeatable; numerical correctness thresholds still require OD-08/OD-07 and suitable core-stability admission. No shared “safety defaults” may silently add damping, raise iteration counts, shrink time steps, disable needed contacts or increase sleep thresholds to make a scene appear stable.

## Construction, ownership and failure model

Use the existing RBS resource/scene pipeline. Selected presets own their complete content; a helper shared by several presets is admitted explicitly, not inherited accidentally from `GeometryGallery`. Preserve existing five-scene content. New chain/ragdoll physics bodies should not require loading unrelated authoring examples or async model demos.

There are two relevant construction stages. First create semantic scene entities, render handles and authored body/shape descriptors using the existing Scene path. After `OnRuntimeStart` has successfully produced typed body bindings, resolve those bindings and create joints/drives through the world API before any Physics tick. A new joint cannot borrow a runtime pointer before its body exists. Freeze that sequencing and the error return path in Phase 26; later joint types reuse it.

Keep per-preset owned identities in a small lifetime record if necessary. An initial-body failure rolls back the existing Scene startup. A later joint/composition failure destroys created joints/controllers, then only bodies/entities owned by that attempt, releases bindings and reports a typed initialization error. World teardown remains the final ownership authority. Duplicate cleanup must be safe without double deletion; invalid cached identities are never dereferenced. These requirements do not mandate a new global registry or transaction framework.

Body deletion retires dependent joints/controllers and filter contributions before storage is freed. Shape/mass/teleport/limit mutations invalidate affected joint caches through the same semantic world route and wake connected bodies when required. Passive ragdoll bone mapping describes relationships; it does not impose local Scene parenting on world-space Physics anchors or write interpolated poses into simulation state.

Debug rendering reuses the existing `DebugGeometry`/Physics debug path. Anchors, axes and limits are drawn from authoritative Physics state with clear distinction from presentation interpolation. Existing cyan Physics, magenta presentation-collider and gold selected-render-mesh meanings remain intact. Selecting a body uses existing typed identity and inspector paths. Debug observation never supplies simulation impulses or concealed stabilization.

## Startup selection now; live replacement only with proven lifecycle

The proposal's supported boundary is **one selected preset per process lifetime**. Relaunching is practical, avoids source edits and preserves existing immutable launch ownership. No hot-switch button, automatic restart or per-frame reinitialization is included in Phases26–32.

A later owner-authorized hot-switch feature would require demonstrated lifecycle evidence covering input/capture/subscription cancellation, active selections, pending imports/tasks, fixed-step backlog/history, controllers/joints before bodies, Scene/world retirement, retained render frames, GPU-resource/context lifetime, partial-create rollback and recovery to a usable scene. It also needs a clear owner for mutable scene selection: mutating the existing borrowed const launch snapshot is not a valid implementation. This list defines missing prerequisites; it does not start a cleanup or stress campaign now. The existence of `OnRuntimeStop` or app teardown alone does not establish safe live replacement.

## Minimum sufficient validation and practical owner review

Initial selector implementation belongs to Phase 26's explicitly proposed production budget. Extend the existing launch-config check for preserved defaults and malformed/unsupported argument handling. Verify available scene names map one-to-one to implemented enum values; selecting an existing preset must preserve its content. Build the affected RBS/GEngine Debug/Release closure once per relevant candidate, with identity-based reuse as allowed.

Use one ordinary no-argument RBS check to preserve `SphereBoxStack`, focused selection/content checks for affected existing paths, and the new phase's selected integration scene(s). Prove that alternate implemented presets and 4/8/16 chain choices are selected from the same built executable without source edits. Check lifetime/cleanup paths implicated by the new joint construction. A GUI picker, exhaustive full-scene replay and extra standalone app are unnecessary. Specialized observers require a material risk that existing deterministic checks cannot resolve.

The future handoff records the exact executable/configuration/working directory, available startup tokens, required inspection actions, automated PASS/FAIL/incomplete/reuse, and residual limitations. Runtime paths remain under `C:/dev/GEngine-pre-editor/runtime/RigidBodySimulation/{Debug,Release}/`; runtime assets and any admitted evidence outputs resolve under the active `OUTPUT_PATHS.json`. Launching from arbitrary IDE defaults does not establish the required output isolation.

Owner inspection remains required for each phase's final candidate. The review explains how to select the relevant scene, observe motion/settling/limits or drive/mover action, inspect debug alignment and close cleanly. A selector success or a screenshot alone does not satisfy quantitative contact, stability, CPU or human gates. No observation in this design is a claim that those future checks have run.

## Mandatory conformance-case coverage

The [chapter traceability register](BOOK_CONFORMANCE_TRACEABILITY.md) requires separate Constant Velocity and Limited Constant Velocity case/evidence identities, resolved before world creation through the admitted Phase28 providers or justified verified equivalents. BookConformance selection cannot merely rename a motor or ball/socket case as proof. Unknown/unimplemented cases are rejected; missing mandatory coverage blocks Phase32 conformance. Preserve ordinary SphereBoxStack startup, typed GeometryGallery default, independent4/8/16 chain configurations and the single existing scheduler. No live switching.
