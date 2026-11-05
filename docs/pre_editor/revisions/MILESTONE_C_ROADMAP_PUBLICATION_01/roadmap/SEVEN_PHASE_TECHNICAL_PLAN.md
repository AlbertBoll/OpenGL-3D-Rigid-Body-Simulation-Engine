# Milestone C: seven accepted roadmap phases

**ACCEPTED ROADMAP DESIGN; NO PHASE STARTED.** This document supplies complete prospective technical contracts for Phases 26–32. It preserves the Phase 26–29 identities and replaces only their future scope descriptions through this accepted amendment. Proposed Phases 30–32 insert Motors, Movers and Book Conformance; former Phases 30–55 move to 33–58 under the recorded governance application. Existing phase files, completed work, failures, receipts, tags and accepted decisions remain unchanged by this package.

All seven phases use the maintained `RigidBodySimulation` application. This is a technical plan, not evidence that joints exist or acceptance gates have passed. The ordinary RBS scene remains `SphereBoxStack`. Startup selection is proposed by extending the **existing** `Rbs::RbsScenePreset`, not introducing a replacement app or duplicate scene identity system. See [RBS scene design](RBS_SCENE_PRESET_DESIGN.md).

## Sources, current architecture and admission

The primary technical reference is Gregory Hodges, *Game Physics: The Rest of Your Life*, version 1.08, copyright 2020, [author's PDF](https://gamephysicsweekend.github.io/pdfs/GamePhysicsTheRestOfYourLife.pdf). The book informs constraint mathematics and demonstration progression; current engine contracts govern ownership, scheduling, errors, transforms and publication. Ball-and-socket is an engine adaptation of the shared point-anchor formulation, not a claim that the book has a named ball-and-socket chapter. See the package's [reference audit](BOOK_REFERENCE.md) for exact chapter/page mapping and limits.

Source inspection finds the following providers:

| Provider | Current implementation and consequence |
| --- | --- |
| `RigidBodySimulation/include/RbsLaunchConfig.h`, `src/RbsLaunchConfig.cpp`, `src/main.cpp` | Existing five-value `RbsScenePreset`; immutable typed `LaunchConfig` borrowed by the app. Ordinary `main.cpp` selects `SphereBoxStack` at compile time. `LaunchConfig{}` independently defaults to `GeometryGallery`; preserve both meanings. Ordinary startup scene argument parsing is not implemented. |
| `RigidBodySimulation/src/RbsScene.cpp` | One scene assembly path, explicit preset switch, semantic render resources, authored physics fixtures, then `StartSceneRuntime`. Geometry/material/shader/async authoring demos are restricted to `GeometryGallery`. New presets must preserve complete scene membership and isolation. |
| `GEngine/include/GEngine/Physics/PhysicsWorld.h/.cpp` | Typed body handles, domain/generation validation, world-owned body/shape lifetime, transactional recoverable errors and owner-thread mutations between fixed ticks. No managed public joint API is present. |
| `GEngine/include/GEngine/Physics/Constraints/Constraint.h` | Legacy raw-body-pointer `PreSolve`/`Solve`/`PostSolve` skeleton. It is mathematical/source material, not a proven public ownership or lifetime provider. |
| `GEngine/include/GEngine/Physics/Constraints/SolverMath.h` | Fixed-storage **contact-only** math, with documented ascending float accumulation to preserve approved contact arithmetic. Joint work cannot silently generalize it or change existing contact order. |
| `GEngine/include/GEngine/Physics/PhysicsSystem.h/.cpp` | Existing fixed-step solver, manifold warm start, ordered contact iterations, contact islands, sleep/wake, CCD revalidation and semantic debug geometry. Joint edges must extend this system's connectivity and scheduling; no second solver or scheduler. |
| `GEngine/src/Scene/_Scene.cpp`, `docs/pre_editor/PHASE_24_TRANSFORM_CONTRACT.md` | Scene owns fixed-step timing, previous/current simulation and presentation separation. Authored pose changes deliberately teleport a live body. Kinematic bodies retain prescribed velocity. Bodies remain world-owned. |

Approved Phase 25 publication is the sequential prerequisite for Phase 26, not a stability waiver. [Phase 21's freeze](/docs/pre_editor/PHASE_21_FREEZE.md) remains `FROZEN_UNAPPROVED`: P21-PEN-01, P21-PEN-02 and P21-L60-DEPTH-01 remain FAIL; Candidate11's rejected visible regression and failed/incomplete solver-cost qualification remain historical failures. No phase in this plan reinstates Candidate11's coupled-contact implementation or reopens that terminated campaign. Later accepted Collider behavior is preserved on its own accepted lineage; it is not confused with the rejected Candidate11 implementation.

**The existing PHY-01/OD-06 joint-entry gate remains closed until matching evidence resolves the resting-contact issue or the owner specifically accepts quantified bounds and rationale suitable for the proposed workloads.** Such an acceptance cannot waive P0 crashes, corruption, ownership/lifetime or integrity failures. Phase 25's accepted game-oriented cost limitations do not establish general solver stability, a universal 60 FPS guarantee, a scaling law or historical non-regression. An unresolved prerequisite is stated as blocked, never translated into PASS by adding new features.

OD-08 must settle, before affected execution, the deterministic scene definitions, admitted feature set, units, durations, joint-error/angular/energy/settling limits, pass counts, contact variants, CPU/allocation boundaries and bounded execution limits. It needs a prospective extension for Motors and Movers; existing wording expressly excludes motors by implication. Roadmap acceptance creates no accepted numeric values for unresolved entry decisions. Historical lattice thresholds remain applicable only to their admitted input/protocol. In particular, 171/180 quiet or sleeping, penetration 0.035 and peak energy 30,391.2 are not generic chain/ragdoll limits.

## Shared contract for the seven phases

The applicable [C0/V0/M0 contracts](VALIDATION_CONTRACTS.md), [active Minimum Sufficient Validation addendum](/docs/pre_editor/active/PHASE_18_PLUS_MINIMUM_SUFFICIENT_VALIDATION.md), accepted toolchain/CRT, typed-error and owner/thread/resource invariants remain binding. Before implementation, freeze a small acceptance matrix naming changed behavior, affected Debug/Release dependency targets, focused checks, reusable evidence, relevant numeric gates, time/workload limits and required owner inspection. Explicit applicable mandatory checks remain mandatory; MSV controls unnecessary campaign expansion, not their removal.

Use affected RBS/GEngine Debug and Release compilation and existing focused deterministic checks. Run only RBS for application integration and human inspection. PhysicsTests or another application is not an automatic gate. Check material changed functionality, lifetime and clean shutdown. Measure performance when materially affected or explicitly required below; these joint additions do affect solver work, so their predeclared targeted body/joint/pass cost checks are retained. No exhaustive Cartesian product, full historical replay, new observer framework or full-solution build is implied. Reuse unaffected PASS only with matching relevant source/dependency/settings identities and an impact argument; old tested product identities remain historical.

Managed joint design is an intended interface contract, not a frozen C++ signature:

- The world owns joints and issues typed slot/generation/domain handles. RBS supplies semantic descriptors containing typed body handles and local anchors/axes. No raw solver pointers cross the consumer boundary.
- Validate finite values, same-world live distinct bodies, supported dynamic/static/kinematic combinations and nondegenerate effective response before publishing a joint. Failure preserves existing state and caller ownership. Static anchors use ordinary explicit static bodies initially; there is no undocumented null-body/world-anchor convention.
- Build rows/effective mass from current body state, warm start once per tick and accumulate impulses with declared bilateral/unilateral bounds. Keep joint cache invalidation explicit for creation/deletion, body retirement, teleport, mass/collider/axis/limit changes and relevant timestep policy. Never retain a pointer or impulse through stale identity reuse.
- Extend the existing connectivity provider with active joint edges for activation and sleep. Preserve contact traversal/arithmetic and CCD behavior. The proposed iteration schedule is existing ordered contact work followed by a stable joint traversal per existing solver pass; freeze and justify that order before implementation. A contact-only workload must not acquire new active joint work. Coupled convergence is checked under admitted gates, not assumed from that ordering.
- `collideConnected` is explicit pair policy, integrated with existing layer/mask filtering. Destroying a joint removes only its own filter contribution; multiple joints cannot accidentally re-enable a still-suppressed pair. Joint errors, body deletion and topology edits cannot leave stale island/filter state.
- Reuse semantic debug drawing for anchors, axes, limits, body identity and sleep/island interpretation. Debug visibility changes observation only; it cannot step Physics or change outcomes. Rendering and picking use the existing presentation contract.

Existing phase budgets are too narrow to assume that joint providers plus runtime scene selection fit unchanged. The following are **accepted roadmap budget amendments**, not phase execution allowlists. New header/source pairs count as two files, RBS changes count as production, and framework-wide refactoring is excluded. Before a later authorized START, freeze exact members inside the accepted envelope; any additional member or semantic expansion requires a reviewed amendment.

| Phase | Accepted production envelope | Supporting envelope | Reason |
| --- | --- | --- | --- |
| 26 | 8–10 | 1–3 | Shared managed joint foundation plus initial startup selection and first complete chain consumer. |
| 27 | 5–8 | 1–3 | Ball/socket provider, existing world dispatch and preset integration. |
| 28 | 5–8 | 1–3 | Hinge plus minimal ragdoll angular limits, existing provider/debug/preset integration. |
| 29 | 6–9 | 1–3 | Small composition provider, lifecycle integration, deterministic ragdoll preset. |
| 30 | 5–8 | 1–3 | Bounded hinge drive using existing joint provider and preset. |
| 31 | 5–8 | 1–3 | Typed prescribed kinematic motion and fixed-tick consumer plus mover preset. |
| 32 | 2–4 RBS selection/configuration members only | 1–3 focused check files, plus compact conformance/review records | Named BookConformance startup preset reusing existing builders, conformance audit and affected qualification; no Physics changes or unbounded remedy allowance. |

Supporting files mean focused validation/build/documentation members; compact required workflow records follow C0's existing accounting. A phase does not have to use its whole envelope. Exact existing/new slots are identified below; names for new providers are design slots, not claims that files exist.

## Phase 26 — Distance Constraints and Chains

**Identity:** existing Phase 26, Milestone C; PHY-04, with PHY-01/PHY-03 and DBG-01 obligations preserved. **Dependency:** approved/published Phase 25; providers 18 and 22–25, with the separately unresolved Phase 21/OD-06 stability admission. OD-08 and applicable OD-07 choices must be settled before their affected entry/checks.

**Functionality and scope.** Introduce world-owned distance joints with two validated local anchors and a positive finite target distance. Exercise scalar Jacobian/effective mass, accumulated impulses, warm start and bounded stabilization in the existing solver. Coincident-anchor/zero-length degeneracy receives a defined typed rejection or specifically admitted formulation; no divide-by-zero fallback or silent spring behavior. Deliver deterministic anchored chains of **4, 8 and 16 dynamic links**, one explicit static anchor and one distance joint per dynamic link. Freeze shapes, masses, spacing, initial orientation/velocity, collision policy and order; count alone is insufficient reproducibility. Add visible anchor/length-error debugging and existing RBS startup selection without rebuilding to change a preset.

**Interfaces.** World-level create/read/destroy distance-joint operations return typed results/handles. Read state supplies anchor positions, error and any admitted impulse/debug data without exporting internal rows. Body destruction retires attached joints before freeing body storage. A change that invalidates effective mass or anchors clears affected caches and wakes the connected group. Joint/shape allocation and partial scene failure have transactional cleanup. Integrate joint connectivity and filtering in the existing system.

**Bounded production slots (maximum 10):** new distance provider header/source pair (2); `PhysicsWorld.h/.cpp` (2); `PhysicsSystem.h/.cpp` (2); `RbsLaunchConfig.h/.cpp` (2); RBS `main.cpp` and `RbsScene.cpp` (2). Keep typed joint identity/descriptor declarations in this admitted provider/world boundary; an additional public file, RBS declaration file or debug provider edit consumes an explicitly revised slot/envelope. Supporting slots: existing launch-config checks, one focused joint check file, phase-specific protocol/review support. Canonical `RigidBodySimulation.cpp` remains a preserved checkpoint member even when unmodified.

**Non-goals.** No ball/socket, hinge, motor, active ragdoll, generic constraint language, contact-law repair, extra solver, arbitrary graph editor or in-process scene switching. No added solver passes, damping or sleep thresholds to hide failures.

**Minimum sufficient validation and exit.** Build affected RBS/GEngine Debug/Release closure. Verify known distance-rest state, loaded motion, invalid/foreign/stale body rejection, warm-start reset, body/joint removal, filter contribution cleanup and connected wake/sleep. In RBS inspect the admitted 4/8/16 chains for target-distance drift, finite states, anchor/render alignment and settling; run only the predeclared contact variants and durations. Measure admitted length-error/energy/settling and CPU versus body/joint/pass count, retaining the existing contact/stability gate. Reuse matching approved body/shape/Transform evidence. Exit requires a visible interactive chain, managed lifetime and all applicable quantitative criteria; algebra alone or a successful launch cannot qualify it. Owner inspects chain motion, settling and anchors before phase approval.

## Phase 27 — Ball-and-Socket Joints

**Identity:** existing Phase 27, Milestone C; PHY-04. **Dependency:** approved/published Phase 26 and its managed lifetime/island/filter provider; retained OD-06 stability admission and OD-08/OD-07 bounds.

**Functionality and scope.** Add a point/ball-and-socket joint that locks three relative anchor translations while preserving all three relative rotational freedoms. Use the same world ownership, accumulated impulse cache, stabilization, filtering and wake model. A three-axis effective-mass formulation must handle singular response through defined validation, not unchecked inversion. Demonstrate a static-anchored free swing and a loaded two-body pair in RBS, with explicit deterministic setup and visible coincident-anchor diagnostics.

**Interfaces.** Add semantic ball/socket descriptor and create/read operations through the existing joint handle/lifecycle route. Local anchors and body frames retain Phase 26 conventions. Extend existing dispatch by joint kind; do not create a parallel allocator, raw-pointer public API or an app-specific solver. Cache reset and body deletion use Phase 26 mechanisms.

**Bounded production slots (5–8):** new ball/socket header/source pair; existing world header/source; `RbsLaunchConfig.h/.cpp`; `RbsScene.cpp`; at most one necessary existing solver/debug integration member. Supporting slots: one focused anchor/rotation/lifetime check, accepted targeted cost protocol and review support.

**Non-goals.** No angular limits, drives, skeleton composition, generic six-degree-of-freedom editor or changes to contact restitution/friction.

**Minimum sufficient validation and exit.** Affected Debug/Release builds; deterministic translation locking/free-rotation and invalid/degenerate/stale identity cases; one loaded pair and one anchored swing; changed teardown and cache transitions; admitted anchor error, energy, convergence and CPU. Reuse Phase 26 lifetime and chain evidence when unchanged; rerun only the affected chain/contact interaction checks if shared math or ordering changes. Exit requires anchor locking without unintended rotational locking, accepted error/cost bounds and no distance-chain regression. Owner inspects free rotation, loaded anchor alignment and settling in RBS.

## Phase 28 — Hinge Joints and Angular Limits

**Identity:** existing Phase 28, Milestone C; PHY-04. **Dependency:** approved/published Phase 27; shared 26/27 provider; retained OD-06 stability admission, OD-08 and applicable OD-07 gates.

**Functionality and scope.** Implement a hinge that keeps anchors coincident, aligns the hinge axes and leaves one relative angular degree of freedom. Add explicit lower/upper angular limits and the **minimum passive angular restriction required by the Phase 29 topology**. Freeze that topology's hinge versus bounded ball/socket requirements under OD-08 before coding. Chapter19 Constant Velocity and Chapter20 Limited Constant Velocity capabilities are mandatory regardless of the selected ragdoll topology. Provide implementations or documented technically equivalent engine constraints with sufficient verification. A generic motor, ordinary ball/socket or label alone is not equivalence; record the reference semantics, supported domain and evidence before claiming coverage.

Specify body-local frames, axis normalization and rejection, signed angle direction, reference orientation, radians, wrap interval and limit inclusivity. Limits are unilateral rows with correct accumulated impulse sign and activation/deactivation; warm-start state must not push a joint farther beyond the wrong limit. Include stable near-boundary behavior without inventing a post-result tolerance.

**Interfaces.** Hinge/limit descriptors, typed limits and read-only angle/error state extend the existing joint API. Validate finite normalized-or-normalizable axes, lower/upper order, supported angular range and changed settings transactionally. Joint axes/allowed arcs use the same semantic debug path. Filter and wake rules remain shared.

**Bounded production slots (5–8):** hinge/limit header/source pair; existing world header/source; `RbsLaunchConfig.h/.cpp`; `RbsScene.cpp`; one necessary solver/debug member. If the mandatory CV/limited-CV coverage or reviewed ragdoll requires additional provider files beyond this accepted envelope, explicitly amend this budget before implementation; do not disguise the pair as zero-cost template work. Supporting slots: focused angle/limit check, admitted quantitative protocol and review support.

**Non-goals.** No motor/drive, animation system, arbitrary joint editor, new contact solver, an unbounded generic joint library beyond the named mandatory Constant Velocity and Limited Constant Velocity capabilities or topology change hidden in testing.

**Minimum sufficient validation and exit.** Affected Debug/Release builds. Free hinge, each lower/upper limit, allowed-region release, a defined wrap-boundary case, invalid axes/limits and loaded-body behavior exercise the changed angular math. Check accumulated-impulse transitions, finite states, sleep/wake, angle/anchor drift, energy and admitted CPU cost. Reuse unchanged Phase 26/27 evidence and current core-stability admission; focused regressions cover shared changes. Exit requires the declared single-axis and limit behavior, plus both named mandatory Constant Velocity capabilities or verified equivalents and the angular capability needed by Phase29. Owner inspects hinge motion and limit compliance with axes/arcs visible.

## Phase 29 — Passive Ragdoll

**Identity:** existing Phase 29, Milestone C; PHY-05/PHY-04, PHY-03 and DBG-01. Its identity and passive deliverable stay fixed; the **final Milestone C exit moves prospectively to Phase 32**. Phase 29 retains every existing passive-ragdoll correctness, settling, performance, API and human gate as its own acceptance gate.

**Dependency:** approved/published Phase 28; providers 18, 22–28; retained OD-06 stability admission and applicable OD-07/OD-08 decisions. Motors and Movers are not prerequisites for passive ragdoll settling.

**Functionality and scope.** Compose ordinary typed bodies/shapes/joints/limits/filtering into one small deterministic passive articulated body. Define a logical parent/child body graph and stable body-to-bone mapping description; use accepted capsule/box/sphere semantics and explicit joint frames. Freeze body count, topology, dimensions, masses/inertias, material/filter policy and limits before execution. Physics bodies stay authoritative in world space: a logical bone hierarchy must not accidentally add a second Scene-parent transform or write presentation poses back into Physics.

Provide all-or-nothing creation of the ragdoll's owned members and deterministic rollback/destruction. Partial failure destroys only members created by that attempt, in joint-before-body order; unrelated world objects survive. Parent-child collision exclusion is explicit and scoped; broader self-collision policy needs its own declared choice. Expose normal drop/settle, one controlled wake/impulse and visible bodies/anchors/axes/limits/sleep/islands in RBS.

**Interfaces.** Small semantic composition descriptor/provider returns a collection/aggregate of existing body/joint identities and a read-only mapping. It owns no independent simulation clock and uses no separate solver. Destroying an individual member either yields a defined degraded aggregate state or atomically retires the aggregate, as selected before execution; dangling mappings are forbidden.

**Bounded production slots (6–9):** new composition header/source pair; existing world header/source if necessary; one Scene lifecycle integration member; `RbsLaunchConfig.h/.cpp`; `RbsScene.cpp`; at most one necessary existing debug/provider member. Supporting slots: focused composition/partial-failure check, admitted settling/cost protocol and review support.

**Non-goals.** No animation/skeleton runtime, skinning, active ragdoll, motors, muscles, IK, blending or get-up controller. No automatic collision masking to hide unstable contact behavior.

**Minimum sufficient validation and exit.** Affected Debug/Release builds; deterministic construction and mapping invariants; one materially representative partial-create failure; member/aggregate destruction and stale-handle rejection; RBS drop, stable settling and controlled wake with accepted filter/limit behavior. Record admitted anchor/angular error, finite states, penetration/energy/sleep and body/joint CPU cost. Retain lattice, box stack, chain and hinge evidence with matching-input reuse and only changed-interaction reruns. Exit requires a visibly useful passive ragdoll that meets all its original gates; remaining C obligations are explicitly assigned to 30–32, never silently dropped. Owner inspects passive motion, settling and debug/body alignment.

## Phase 30 — Motors

**Identity:** new accepted roadmap Phase 30, Milestone C; formerly optional motor capability becomes required only through explicit acceptance/application of this amendment. **Dependency:** approved/published Phase 29; hinge/limit provider 28 and world/island/lifetime provider 26. Retained stability admission and extended OD-08/OD-07 decisions apply.

**Functionality and scope.** Add one bounded angular-velocity drive to an existing hinge. Specify signed target relative angular velocity, enabled state and finite maximum torque; translate the torque bound to per-step impulse using the existing fixed step. Apply only the permitted clamped change in accumulated impulse. Limits take precedence: the motor cannot defeat an active stop. Include disabled, zero target, forward/reverse and loaded/stalled behavior within an admitted envelope. The book's Chapter 22 motivates the drive; finite force/torque limits, typed configuration and engine ownership are explicit adaptations.

**Interfaces.** Motor settings belong to the existing hinge/joint identity. Setting, disabling or changing target uses a transactional owner-thread operation and defined cache/wake semantics. Invalid settings leave state unchanged. Destroying the joint/body removes its drive. Read state distinguishes commanded velocity, actual relative velocity and bounded effort; it does not pretend that saturation reaches the target.

**Bounded production slots (5–8):** existing hinge header/source; world header/source; `RbsLaunchConfig.h/.cpp`; `RbsScene.cpp`; at most one necessary solver/debug integration member. Supporting slots: focused drive/limit/saturation check, admitted cost/work protocol and review support.

**Non-goals.** No position-servo tuning framework, robotics controller, linear-motor catalogue, active ragdoll, animation blending or global damping/iteration changes. Passive ragdoll remains passive.

**Minimum sufficient validation and exit.** Affected Debug/Release builds. Deterministic disabled equivalence, direction/reversal, target/effort clamping, limit conflict, loaded stall and stale-handle cleanup. RBS shows the motor starting, reversing, reaching a limit or saturating under load, then disabling. Track finite state, constraint error, CPU and admitted actuator work/energy bounds. A motor deliberately injects energy: passive energy-conservation criteria are not applied as if external work were zero, and unexplained energy growth is not excused by calling it powered. Reuse passive joint/ragdoll evidence only where shared changes cannot affect it; otherwise run affected checks. Exit requires bounded controllable drive and preserved passive/limit/contact behavior. Owner inspects direction, force limitation, stop/disable and clean shutdown.

## Phase 31 — Movers

**Identity:** new accepted roadmap Phase 31, Milestone C. **Dependency:** approved/published Phase 30; typed body provider 22, Transform/clock provider 24, contact behavior on the accepted current lineage and shared 26–30 integration. Retained OD-06 admission and extended OD-08/OD-07 gates apply.

**Functionality and scope.** Provide one deterministic prescribed kinematic platform/mover using the current body-type and single fixed-tick model. Start with a bounded linear path and explicit start/stop/reverse behavior; rotating motion is included only if the accepted feature envelope names it and its angular/CCD implications fit the budget. The book's Chapter 23 sets velocity on an infinite-mass body; the engine adaptation uses semantic `Kinematic` bodies, typed velocity/target commands and finite validated parameters. It does not make a moving object `Static` merely by changing inverse mass.

Commands are applied at the existing owner-thread fixed-tick boundary; a scripted trajectory reads simulation ticks/time, never render delta or wall-clock time. Prefer the existing typed velocity operation when it expresses the admitted motion. If a pose-target API is needed, freeze its target-time and velocity derivation contract before implementation. Do not implement platform motion by per-render-frame `SetLocalPose`: under Phase 24 that is an authoring teleport with history/cache invalidation, not ordinary kinematic integration.

**Interfaces.** A small semantic mover configuration/controller binds a typed kinematic body identity, trajectory values and enable/stop state. It neither owns a second world nor advances the scheduler. Dynamic and wrong-world/stale bodies are rejected. Pausing freezes the simulation-time trajectory. Stopping yields a defined zero prescribed velocity and proper contact/wake response; discontinuous reset is explicit and uses the existing teleport/history rules. Deletion retires the controller before body storage.

**Bounded production slots (5–8):** one small mover provider pair only if necessary; existing world or Scene scheduling boundary members selected from `PhysicsWorld.h/.cpp` and `_Scene.cpp`; `RbsLaunchConfig.h/.cpp`; `RbsScene.cpp`; a necessary existing PhysicsSystem integration member only within the accepted total. Exact selection is required because selecting every locator exceeds the envelope. Supporting slots: fixed-tick trajectory/contact/lifetime check, admitted CPU/work protocol and review support.

**Non-goals.** No path editor, navigation, character controller, rider attachment hacks, infinite-force dynamic servo, conveyor belt friction model, collision/CCD rewrite or scene-switch framework. Mover/rider friction and collision behavior remain the existing accepted physical model.

**Minimum sufficient validation and exit.** Affected Debug/Release builds. Verify prescribed trajectory at simulation ticks, pause/resume and stop/reverse, two supported render-update schedules with equivalent fixed-step histories, typed rejection and cleanup. In RBS exercise one supported platform contacting/pushing/carrying ordinary dynamic bodies according to the admitted friction/material setup; include bounded wake/settle after stopping. Measure finite state, declared trajectory/contact/penetration errors, relevant external work/energy and CPU. Contact failures retain their actual classification and gates; no speculative tuning or Candidate11 restoration. Exit requires usable prescribed motion, preserved fixed-step/presentation semantics and accepted contact/lifetime/cost behavior. Owner inspects motion, rider/contact response, pause/stop and clean shutdown.

## Phase 32 — Book Conformance and Milestone C Exit

**Identity:** new accepted roadmap Phase 32, final Milestone C gate. **Dependency:** approved/published Phase 31 and applicable approved providers 18–30; Phase 21's frozen history is not converted to approved provenance. All due OD-06/OD-07/extended OD-08 decisions and explicitly deferred C obligations must have their required technical evidence or permissible specific owner disposition.

**Functionality and scope.** Produce an auditable book-to-engine conformance matrix covering foundational rigid-body/collision providers, distance/chains, ball/socket adaptation, hinge/angular formulations, passive ragdoll, motors and movers. For each relevant topic name book chapter/page, current provider, retained behavior, documented adaptation (no omission of mandatory capabilities), observable check/evidence identity and claim limit. Chapters 12–13's penetration/manifold treatment is reference material for the existing accepted contact model, not authority to reimplement it. Distinguish behavioral/mathematical correspondence from source transcription, identical algorithms, binary equality or universal physical correctness.

Add a named `BookConformance` startup preset with token `book-conformance` and a required `--case` selection. Resolve that case before world creation to one existing admitted scene builder/configuration; preserve both requested case and resolved scene identity. For example, `--scene book-conformance --case distance-chain-8` selects the existing eight-link chain and its book/engine conformance ledger row. Freeze the small supported case list from the actual chapter/deviation matrix, reject unknown or unimplemented cases, and avoid a giant combined scene, hot-switch loop or automatic multi-case campaign. Book conformance cases make the relevant demonstrations discoverable without duplicating their Physics implementation.

Consolidate existing successful evidence by relevant identity and dependency impact. Perform only the smallest additional affected deterministic check or RBS integration necessary to close a specific conformance gap and the still-binding C exit. The conformance selector reuses the existing startup builders. Reconcile correctness, settling, constraints, ragdoll, motor/mover, CPU/scaling claims, API usability, debug and Transform/rendering obligations. Keep any limited accepted claim labeled as such.

**Interfaces and budget.** Propose 2–4 RBS production members for the existing enum/configuration/parser and scene dispatch (`RbsLaunchConfig.h/.cpp`, `RbsScene.cpp` and one necessary RBS declaration/bootstrap member). This authorizes only the future reviewed conformance selector implementation; no Physics algorithm, ownership or public body/joint API change is included. Use 1–3 focused existing/new validation files if an identified gap warrants them, plus the compact conformance/exit review. A demonstrated Physics production defect blocks its affected gate; propose a bounded corrective amendment/revision before changing Physics. No full Physics rewrite, fresh historical comparison campaign or opportunistic cleanup is hidden under “conformance.”

**Minimum sufficient validation and exit.** Verify the matrix's citations, provider identities, source/input coverage, quantitative gate status and explicit adaptations. Compile the affected RBS selector in Debug/Release, reusing matching unchanged GEngine/dependency evidence where valid. Check required-case validation, unsupported-case rejection and correct resolution to existing scene configuration; demonstrate a representative conformance startup case. RBS owner inspection of the final integrated candidate covers the required existing/new capabilities through selectable presets. Other additional runtime is justified by a concrete uncovered interaction, not a blanket replay of every earlier phase. Unchanged historical binary PASS is not claimed for a newly rebuilt product.

The C exit requires every applicable original technical gate plus the explicitly added motor/mover and book-conformance claims; permitted owner bounds remain specific. P0 always blocks. Phase32 cannot declare book conformance while any mandatory capability, correctness, stability or performance gate remains unresolved. A bounded risk disposition or P2 deferral does not substitute for mandatory capability delivery/verified equivalence or sufficient validation. Existing severity/disposition rules retain their own scope and do not erase this conformance block. Bounded P2 residuals disclose impact, likelihood, recovery, evidence, residual owner and revisit trigger. No original FAIL/INCONCLUSIVE becomes PASS by consolidation. Lighting starts at proposed Phase 33 only after C exit and explicit subsequent owner START; no automatic transition.

## Deferred obligations and numbering continuity

This table records accepted prospective ownership, not technical closure. Historical paths and phase numbers inside immutable records retain their original meaning; a mapping points from them to future owners instead of rewriting provenance.

| Obligation | Existing gate/owner | Proposed future responsibility |
| --- | --- | --- |
| PHY-01 / Phase 21 failures / OD-06 | Blocks Phase 26–29 joint expansion and C exit | Preserve the Phase 26 entry block and apply the suitable-workload admission to all seven phases; final C reconciliation at 32. No automatic remediation phase or waiver. |
| PHY-03 / OD-07 / accepted Phase 25 claim limitations | Joint cost and C exit; limited current-cost evidence | Phase-specific affected cost in 26–31; exact limited-claim reconciliation at 32. Historical causal/non-regression obligations stay at their independently assigned later gates. |
| PHY-04 | Distance 26, ball/socket 27, hinge/limits 28; C exit 29 | Same providers26–28 and their own gates; final C exit 32. |
| PHY-05 | Passive ragdoll29 | Same passive provider 29; no motor dependency. Its gates remain at 29 and feed exit 32. |
| Debug/semantic API/Transform integration | Providers18,22–24 and dependent C gates | Reuse/extend only affected paths26–31; reconcile at 32. No new parallel rendering/debug/property framework. |
| P24-PERF-CPU / P24-PERF-REPLACE and Phase 25 narrower claims | Specific accepted dispositions and applicable later historical-lineage obligation, former 53 | Preserve actual statuses/conditions; future 56 owns the former 53 residual work. Phase 32 cannot silently declare it completed. |
| Renderer/GPU/delivery/zero-work/memory residuals, including PERF65-04 | Former40 and its providing D phases | Proposed43 and shifted D providers; do not add a joint-phase rendering campaign. |
| PERF65-01 | Former52 | Proposed55 with unchanged staged comparability and quantitative criteria. |
| PERF65-02 and PERF65-05-06 | Former53 | Proposed56 with unchanged lineage/comparability and original limits. |
| PERF65-03 / accepted async carry-forward | Phase 12 and its actual later accepted obligations | Phase 12 history unchanged; follow each extant disposition by identity, shift only any future owner originally 30–55 by +3. No invented reopening or closure. |
| Integrated and final editor handoff | Former54–55 | Proposed57–58; retain all original required checks and all five residual families. |
| Future Lighting/Editor providers | Former30–55 | Shift every future contract/dependency/decision/owner reference by +3, preserving identity mapping and original technical scope. Historical receipts/tags are never renumbered. |

The package's complete mapping and successor contracts own the exhaustive renumbering. The recorded roadmap application reconciles route bounds and conditional publication eligibility through58. No phase execution or roadmap publication is authorized; every START/APPROVE retains its explicit command and applicable entry gates.

## Required handoff and present status

Each future authorized phase performs only its admitted work, preserves failures, reuses unaffected evidence and stops after a concise functionality/changed-files/PASS/FAIL/incomplete/reuse/residual review with **READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.** The owner inspects the final candidate; explicit `APPROVE PRE_EDITOR PHASE XX` alone authorizes the unchanged internal seal then guarded finish/publication workflow. No standalone SEAL, self-approval, premature publication, helper bypass or next-phase launch is introduced.

**Present result: accepted design only; implementation not authorized.** No Debug/Release build, runtime, joint qualification, human acceptance, phase execution, seal, commit, tag or push is authorized by the design. Its validation is static document/source/reference consistency only.

## Applied controlling clarification

[Owner clarification](OWNER_CLARIFICATIONS.md) and [chapter traceability](BOOK_CONFORMANCE_TRACEABILITY.md) are mandatory contract inputs for affected work. Exact joint-entry and numerical decisions remain separately unresolved in the [Phase26 review](/docs/pre_editor/active/MILESTONE_C_PHASE_26_ENTRY_DECISION_REVIEW_01.md). The original reviewed package remains immutable.
