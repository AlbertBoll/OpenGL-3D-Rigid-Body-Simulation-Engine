# PRE_EDITOR accepted successor roadmap - Milestone C amendment 01

Accepted and applied prospectively under [Owner clarification](OWNER_CLARIFICATIONS.md) and `/docs/pre_editor/active/MILESTONE_C_ROADMAP_ACTIVATION_01.json`. Roadmap authority is active; every unstarted phase remains NOT_STARTED. Implementation and roadmap publication are not authorized.

## Milestone exit boundaries

| Lane | Phases | Exit |
| --- | --- | --- |
| Setup / Code Quality / A / B | 00-17 | Existing approved history and obligations unchanged |
| C Physics | 18-32 | Existing contact/stability/API/shape/Transform/debug/CPU obligations plus distance/chain, ball/socket, hinge/limits, required Constant Velocity and Limited Constant Velocity capabilities or verified equivalents, passive ragdoll, motors, movers and conformance accepted |
| D Lighting/PBR/HDR | 33-43 | Former30-40 requirements unchanged |
| E authoring | 44-54 | Former41-51 requirements; motor/mover metadata/persistence/transactions/isolation/debug added to existing owners |
| F integrated handoff | 55-58 | Original residuals, full RBS authoring/runtime and final owner handoff; motors/movers included |

59 review units. Phases26-29 preserve identity;30-32 are inserted; every former30-55 maps once to old+3. No approved identity, tag, receipt, evidence or historical link is renamed. Numeric commands refer only to the newly activated index; old-number aliases are forbidden.

## Phase and dependency index

Every new phase requires the immediately preceding approval/publication. Additional providers below point backward. Phase21 remains a frozen obligation requiring explicit joint-entry resolution; its appearance never asserts approval.

| Phase | Goal | Lane | Additional providers | Old identity |
| --- | --- | --- | --- | --- |
| [00](/docs/pre_editor/phases/PHASE_00.md) | Minimal isolated activation and workflow setup | Setup | Rendering68 | 00 |
| [01](/docs/pre_editor/phases/PHASE_01.md) | Adopt scoped Code Quality policy | Code Quality | 00 | 01 |
| [02](/docs/pre_editor/phases/PHASE_02.md) | Mechanical formatting of initial A touchpoints | Code Quality | 01 | 02 |
| [03](/docs/pre_editor/phases/PHASE_03.md) | Bounded resource and RBS readability cleanup | Code Quality | 02 | 03 |
| [04](/docs/pre_editor/phases/PHASE_04.md) | Minimum runtime Scene operations and shared assignment | A | 03 | 04 |
| [05](/docs/pre_editor/phases/PHASE_05.md) | Resource ownership and renderer-private frame preparation | A | 04 | 05 |
| [06](/docs/pre_editor/phases/PHASE_06.md) | CPU simple geometry templates and canonical sharing | A | 04, 05 | 06 |
| [07](/docs/pre_editor/phases/PHASE_07.md) | Parametric curved geometry templates | A | 06 | 07 |
| [08](/docs/pre_editor/phases/PHASE_08.md) | Explicit geometry copy, MakeUnique and dimension/Bake semantics | A | 04, 06, 07 | 08 |
| [09](/docs/pre_editor/phases/PHASE_09.md) | Typed standard material templates | A | 04, 05 | 09 |
| [10](/docs/pre_editor/phases/PHASE_10.md) | Shader descriptions, reflection and bounded variants | A | 05, 09 | 10 |
| [11](/docs/pre_editor/phases/PHASE_11.md) | Explicit shader/material reload and atomic replacement | A | 05, 09, 10 | 11 |
| [12](/docs/pre_editor/phases/PHASE_12.md) | Async resource adoption and upload-stall evidence | A | 04, 05, 06, 07, 08, 09, 10, 11 | 12 |
| [13](/docs/pre_editor/phases/PHASE_13.md) | Typed subscriptions, lifetime and dispatch semantics | B | 12 | 13 |
| [14](/docs/pre_editor/phases/PHASE_14.md) | Authoritative runtime Transform mutation and successful-change notifications | B | 04, 08, 13 | 14 |
| [15](/docs/pre_editor/phases/PHASE_15.md) | Complete typed input state and routing | B | 13, 14 | 15 |
| [16](/docs/pre_editor/phases/PHASE_16.md) | Unified semantic camera snapshot and conversions | B | 14, 15 | 16 |
| [17](/docs/pre_editor/phases/PHASE_17.md) | Scene Camera and Editor view lifecycle adoption | B | 15, 16 | 17 |
| [18](/docs/pre_editor/phases/PHASE_18.md) | Shared semantic debug rendering foundation | C | 12, 17 | 18 |
| [19](/docs/pre_editor/phases/PHASE_19.md) | Resting-contact and impact diagnosis in RBS | C | 18 | 19 |
| [20](/docs/pre_editor/phases/PHASE_20.md) | Bounded resting-contact and impact ownership correction | C | 19 | 20 |
| [21](/docs/pre_editor/phases/PHASE_21.md) | Convergence, sleep and fixed-step qualification | C | 20 | 21 |
| [22](/docs/pre_editor/phases/PHASE_22.md) | Typed Physics world and body API ownership | C | 14, 21 | 22 |
| [23](/docs/pre_editor/phases/PHASE_23.md) | Independent collision shapes including Capsule | C | 22 | 23 |
| [24](/docs/pre_editor/phases/PHASE_24.md) | Physics, Scene and Rendering Transform contract | C | 14, 16, 22, 23 | 24 |
| [25](/docs/pre_editor/phases/PHASE_25.md) | Physics CPU and scaling acceptance | C | 21, 24 | 25 |
| [26](phases/PHASE_26.md) | Distance Constraints and Chains | C | 18, 21, 22, 23, 24, 25 | 26 |
| [27](phases/PHASE_27.md) | Ball-and-Socket Joints | C | 26 | 27 |
| [28](phases/PHASE_28.md) | Hinge Joints and Angular Limits | C | 26, 27 | 28 |
| [29](phases/PHASE_29.md) | Passive Ragdoll | C | 18, 21, 23, 24, 25, 26, 27, 28 | 29 |
| [30](phases/PHASE_30.md) | Motors | C | 22, 24, 26, 28, 29 | inserted |
| [31](phases/PHASE_31.md) | Movers | C | 22, 23, 24, 25, 26, 30 | inserted |
| [32](phases/PHASE_32.md) | Book Conformance | C | 18, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 | inserted |
| [33](phases/PHASE_33.md) | Shared material/light semantics and GPU representation | D | 09, 16, 32 | 30 |
| [34](phases/PHASE_34.md) | Shared Direct PBR and light-specific evaluation | D | 33 | 31 |
| [35](phases/PHASE_35.md) | Directional and Point shadow quality and invalidation | D | 16, 34 | 32 |
| [36](phases/PHASE_36.md) | Spot shadow projection and owned resources | D | 33, 35 | 33 |
| [37](phases/PHASE_37.md) | Spot caster pass and per-light shadow visibility | D | 34, 36 | 34 |
| [38](phases/PHASE_38.md) | IBL environment processing and resource publication | D | 05, 10, 34, 37 | 35 |
| [39](phases/PHASE_39.md) | Indirect PBR integration | D | 33, 34, 38 | 36 |
| [40](phases/PHASE_40.md) | Linear HDR scene color and resolve | D | 34, 37, 39 | 37 |
| [41](phases/PHASE_41.md) | Energy-aware multiresolution Bloom | D | 40 | 38 |
| [42](phases/PHASE_42.md) | Manual exposure, tone mapping and final display | D | 16, 40, 41 | 39 |
| [43](phases/PHASE_43.md) | Lighting/shadow default profile and D acceptance | D | 33, 34, 35, 36, 37, 38, 39, 40, 41, 42 | 40 |
| [44](phases/PHASE_44.md) | Complete Scene/ECS authoring and hierarchy lifecycle | E | 04, 14, 24, 43 | 41 |
| [45](phases/PHASE_45.md) | Project roots and persistent identity | E | 44 | 42 |
| [46](phases/PHASE_46.md) | Shared property metadata and structured authoring diagnostics | E | 09, 13, 29, 30, 31, 32, 43, 44, 45 | 43 |
| [47](phases/PHASE_47.md) | Scene/component persistence and semantic round trip | E | 44, 45, 46 | 44 |
| [48](phases/PHASE_48.md) | Constraint and ragdoll persistence extensions | E | 26, 27, 28, 29, 30, 31, 32, 47 | 45 |
| [49](phases/PHASE_49.md) | AssetDatabase, import settings and observable tasks | E | 12, 45, 46, 47 | 46 |
| [50](phases/PHASE_50.md) | Reimport dependencies, rollback and last-valid notifications | E | 11, 13, 49 | 47 |
| [51](phases/PHASE_51.md) | Authoritative transactions and grouped mutations | E | 13, 14, 30, 31, 32, 44, 46, 47, 48, 50 | 48 |
| [52](phases/PHASE_52.md) | Undo/Redo and dirty/saved revisions | E | 30, 31, 32, 47, 48, 51 | 49 |
| [53](phases/PHASE_53.md) | Edit/Play state and resource isolation | E | 13, 24, 30, 31, 32, 44, 50, 51, 52 | 50 |
| [54](phases/PHASE_54.md) | Extend shared authoring/debug visualization and E exit | E | 18, 29, 30, 31, 32, 43, 44, 46, 53 | 51 |
| [55](phases/PHASE_55.md) | RBS admission and qualification of original C scaling residual | F | 12, 43, 54 | 52 |
| [56](phases/PHASE_56.md) | Historical CPU lineage and causal attribution | F | 55 | 53 |
| [57](phases/PHASE_57.md) | Full RBS authoring-to-runtime integration | F | 12, 17, 29, 30, 31, 32, 43, 54, 55, 56 | 54 |
| [58](phases/PHASE_58.md) | Core Editor handoff contract and final acceptance | F | 57 | 55 |

## Deferred obligations and owner decisions

See FINDINGS.md and PERFORMANCE_RESIDUALS.md for prospective owners; historical technical statuses are not changed by renumbering. See VALIDATION_CONTRACTS.md for C0/V0/M0, OD-08 expansion and distinct motor/mover/conformance decisions. No A-D exit waits for E. Startup scenes are delivered with each feature; later E owns persistent authoring and F owns integrated handoff.

Core Editor UI, topology editing, active ragdoll, animation blending, IK, muscles/get-up, pose servos, runtime scene hot-switching, spline authoring, Area lighting and auto exposure stay deferred. Motors and movers alone are promoted into required C scope.

The exact seven-phase design and audited RBS startup architecture are packaged with this roadmap. Review completion is not implementation readiness.
