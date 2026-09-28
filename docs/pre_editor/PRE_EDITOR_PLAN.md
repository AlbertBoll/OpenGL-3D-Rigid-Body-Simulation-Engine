# Proposed PRE_EDITOR plan

**PROPOSED / NOT_STARTED. Audit Completion: COMPLETE_FOR_REVIEW. Plan Readiness: READY_FOR_HUMAN_REVIEW. Milestone A Readiness: NOT_READY.**

This package completes the requested audit/planning task. It proposes work; it does not activate PRE_EDITOR, approve implementation, create workflow receipts, or change Rendering history. Review begins here, then follows finding/phase links as needed. Future phases load their compact contract, applicable validation and targeted source, not the entire master instruction by default.

[Revision 01](revisions/REVISION_01.md) clarifies Phase 00 bootstrap entry, supported scale/collider behavior and Phase 52–53 admission timing. Phase numbering, milestone scope, residual ownership and original audit observation timestamps remain unchanged. This revision grants no activation authorization.

## Verified entry and current limits

Rendering Phase 68 commit **9995074e7af161db76c5bf79a5ede030702fb4ca** on rendering/refactor is approved, committed, tagged as render-refactor-phase-68-approved, branch/tag published to AlbertBoll/OpenGL-3D-Rigid-Body-Simulation-Engine, and recorded COMPLETE. Local HEAD, tag/remote identities and completed journal agree; all 68 present approval journals are complete. This is both the immutable Rendering reference and proposed PRE_EDITOR entry. See [audit evidence](HANDOFF_AUDIT.md#entry-gate-and-exact-baselines).

Current local changes are preserved separately and are not an approved baseline. No new Rendering completion defect was established. Current authoritative Physics is the Phase 68 Physics tree 829629515a5363bdcf23d6f1654e7714a5fa9f18 plus its Scene integration. Approved Physics Phase 39, 89030282c1b2fd21a55eae2934e7f080eff7950d, is already an ancestor. The separate physics/refactor worktree remains at that commit with unapproved edits (including default solver passes 2 versus authoritative 1); rejected Phase 40 implementation is not imported.

No new applications, tests, builds, benchmarks or diagnostics were executed. Future new application-level validation uses **RigidBodySimulation only**. Matching approved evidence is reused with its limits; source inspection is not a new runtime/performance PASS.

## Scope and gates

| Lane | Proposed phases | Concrete exit |
| --- | --- | --- |
| Minimal setup | 00 | Separately authorized isolated branch/worktree, document provenance, output isolation and compact workflow adaptation |
| Code Quality | 01–03 | Scoped policy, mechanical formatting, separately reviewed semantic readability; CQ-01 closes before A |
| Evidence-required pre-A performance remediation | **None** | No demonstrated pre-A implementation blocker; no placeholder optimization phase |
| A resource/shader/material/geometry | 04–12 | Simple semantic creation/editing, shared geometry/duplicate/MakeUnique/stretch/Bake, bounded shaders/reload and correct resource lifetime without manager chains |
| B event/input/camera | 13–17 | Safe subscriptions/routing and successful-change operations; complete input; coherent Scene/Editor camera and picking |
| C Physics | 18–29 | Shared debug provider; core rest/impact stability before joints; typed body/collider APIs and Transform flow; chain, ball/socket, hinge/limits, passive ragdoll and qualified CPU evidence |
| D Lighting/PBR/HDR | 30–40 | Shared Direct PBR, Directional/Point/Spot with all three shadows, IBL, linear HDR, required physically based Bloom, manual exposure/tone/display and visual/temporal/performance acceptance |
| E authoring | 41–51 | Semantic Scene/ECS/Project/persistent IDs/serialization/AssetDatabase/tasks, shared metadata/diagnostics, reimport, transactions/Undo/dirty state, Edit/Play and shared overlays |
| F integrated handoff | 52–55 | All original performance-family dispositions and full RBS authoring-to-runtime flow accepted; Core Editor receives stable contracts |

There are 56 proposed review units because the source gaps divide into distinct ownership changes and validation gates: initial mechanical versus semantic cleanup; runtime versus persistent foundations; diagnosis versus correction/qualification; joint progression; shadow resources versus visibility; IBL processing versus integration; HDR versus Bloom versus display; metadata versus persistence versus transactions. The count is not a quota. Exact file allowlists are admitted before START; a materially broader change needs an explicit amendment/numeric split. Do not hide a whole rewrite inside a nominal six-file budget. Empty remediation phases and separate A01/Physics01/Lighting01 workflows are intentionally absent.

No A–D milestone exit depends on implementing E. A–E capabilities are adopted in RBS when delivered; F integrates them instead of deferring all consumer work.

## Earliest-consumer provider table

| Shared foundation | Earliest consumer | Current provider / inspected evidence | Missing minimum / providing phase | Later extension owner |
| --- | --- | --- | --- | --- |
| Runtime Scene/Entity and sharing | A assignment/duplicate, 04 | _Scene create/duplicate/copy and MeshRenderer handles already share resources, clear runtime bindings; S09 | 04 narrow create/assign/duplicate with typed failure/revision result; no new ECS | 41 full authoring lifecycle/hierarchy; 42 IDs; 44/48 persistence/transactions |
| Typed material/property intent | A standard creation, 09 | MaterialTemplate/Instance exist; RBS uses string names; S05/S08/S18 | 09 semantic fields/templates plus minimum fixed built-in program cache; 10 custom shader descriptions/reflection/finite variants | 30–31 full shared PBR on same model; 43 metadata, 44/48 persistence/transactions |
| Mutation and successful-change notification | B consumers, 14 | Changed SetScale emits the pre-store callback with the new scale; direct Scale writes do not. The live Physics bridge has shape-specific effects; S09/S11/S22 | 13 preserves supported callback effects while migrating lifetime; 14 deliberately separates internal collider update ordering from public notification after successful mutation; no early Undo/transaction claim | 48 validate/apply/commit/rollback and committed-only events |
| Runtime Transform/hierarchy | A scale uses existing local TRS; B camera 16 and C integration 24 | Scene local/world cache, fixed-step poses and supported connected setter-to-shape bridge, including base-source initialization; S09/S10/S22 | 08 preserves supported Transform/collider scaling and distinguishes regeneration/Bake/explicit collider edits; 13–14 deliberately migrate lifetime/mutation; 24 integrates shape revisions/cache/wake and body flow | 41 complete preserve-world/local authoring, 44 persistence, 50 Edit/Play |
| Semantic debug rendering | C diagnostics, 19 | Frame payload exists but FrameSubmission rejects nonempty lines; S16 | 18 smallest working bounded semantic line/shape submission, then joint overlays in 26–29 | 51 icon/billboard/frustum/grid and complete authoring views on same path |
| Typed structured operation diagnostics | A creation/reload, 04–12 | SceneError, Shader/Material/Mesh/Physics expected errors and async states exist; S02/S04/S05/S13 | Reuse contextual typed subsystem errors in each A–D operation; no early generic DiagnosticsManager | 43 lightweight shared severity/subsystem/code/context projection; 46 observable tasks |
| Runtime resource/version ownership | A APIs, 05 | AssetRegistry/Publication, scheduler, leases/fences/context root; S01/S06/S07 | 05 renderer-owned frame preparation and narrow operations; no new registry | E AssetDatabase describes persistent provenance, never replaces runtime ownership |
| Physics joint execution/lifetime | C chain, 26 | Contact constraint base/math/islands exist; no managed joints; S14/S15 | 22 typed body identity then 26 managed distance/chain in same solver; 27 ball/socket; 28 hinge/limits | 29 passive ragdoll; 45 persistence; 48/49 property history |

These providers are either the approved entry or earlier phases. In particular, typed legacy Signal/Property is not assumed to satisfy safe subscription or committed mutation semantics. ShaderManager is a small legacy cache, not evidence of a universal god object; modern repeated compilation and broad SceneRenderResources exposure are the specific consolidation targets.

## Numeric phase and dependency index

Every phase requires the immediately preceding phase to be actually approved and published. The last column lists additional provider dependencies and the first entry baseline; all numeric edges point backward. A–F are milestones, not independent workflow identities.

| Phase | Goal | Lane | Additional providers |
| --- | --- | --- | --- |
| [00](phases/PHASE_00.md) | Minimal isolated activation and workflow setup | Setup | Phase 68 |
| [01](phases/PHASE_01.md) | Adopt scoped Code Quality policy | Code Quality | 00 |
| [02](phases/PHASE_02.md) | Mechanical formatting of initial A touchpoints | Code Quality | 01 |
| [03](phases/PHASE_03.md) | Bounded resource and RBS readability cleanup | Code Quality | 02 |
| [04](phases/PHASE_04.md) | Minimum runtime Scene operations and shared assignment | A | 03 |
| [05](phases/PHASE_05.md) | Resource ownership and renderer-private frame preparation | A | 04 |
| [06](phases/PHASE_06.md) | CPU simple geometry templates and canonical sharing | A | 04, 05 |
| [07](phases/PHASE_07.md) | Parametric curved geometry templates | A | 06 |
| [08](phases/PHASE_08.md) | Explicit geometry copy, MakeUnique and dimension/Bake semantics | A | 04, 06, 07 |
| [09](phases/PHASE_09.md) | Typed standard material templates | A | 04, 05 |
| [10](phases/PHASE_10.md) | Shader descriptions, reflection and bounded variants | A | 05, 09 |
| [11](phases/PHASE_11.md) | Explicit shader/material reload and atomic replacement | A | 05, 09, 10 |
| [12](phases/PHASE_12.md) | Async resource adoption and upload-stall evidence | A | 04, 05, 06, 07, 08, 09, 10, 11 |
| [13](phases/PHASE_13.md) | Typed subscriptions, lifetime and dispatch semantics | B | 12 |
| [14](phases/PHASE_14.md) | Authoritative runtime Transform mutation and successful-change notifications | B | 04, 08, 13 |
| [15](phases/PHASE_15.md) | Complete typed input state and routing | B | 13, 14 |
| [16](phases/PHASE_16.md) | Unified semantic camera snapshot and conversions | B | 14, 15 |
| [17](phases/PHASE_17.md) | Scene Camera and Editor view lifecycle adoption | B | 15, 16 |
| [18](phases/PHASE_18.md) | Shared semantic debug rendering foundation | C | 12, 17 |
| [19](phases/PHASE_19.md) | Resting-contact and impact diagnosis in RBS | C | 18 |
| [20](phases/PHASE_20.md) | Bounded resting-contact and impact ownership correction | C | 19 |
| [21](phases/PHASE_21.md) | Convergence, sleep and fixed-step qualification | C | 20 |
| [22](phases/PHASE_22.md) | Typed Physics world and body API ownership | C | 14, 21 |
| [23](phases/PHASE_23.md) | Independent collision shapes including Capsule | C | 22 |
| [24](phases/PHASE_24.md) | Physics, Scene and Rendering Transform contract | C | 14, 16, 22, 23 |
| [25](phases/PHASE_25.md) | Physics CPU and scaling acceptance | C | 21, 24 |
| [26](phases/PHASE_26.md) | Managed distance constraints and representative chain | C | 18, 21, 22, 23, 24, 25 |
| [27](phases/PHASE_27.md) | Ball-and-socket constraints | C | 26 |
| [28](phases/PHASE_28.md) | Hinge constraints and angular limits | C | 26, 27 |
| [29](phases/PHASE_29.md) | Passive ragdoll composition and C exit | C | 18, 21, 23, 24, 25, 26, 27, 28 |
| [30](phases/PHASE_30.md) | Shared material/light semantics and GPU representation | D | 09, 16, 29 |
| [31](phases/PHASE_31.md) | Shared Direct PBR and light-specific evaluation | D | 30 |
| [32](phases/PHASE_32.md) | Directional and Point shadow quality and invalidation | D | 16, 31 |
| [33](phases/PHASE_33.md) | Spot shadow projection and owned resources | D | 30, 32 |
| [34](phases/PHASE_34.md) | Spot caster pass and per-light shadow visibility | D | 31, 33 |
| [35](phases/PHASE_35.md) | IBL environment processing and resource publication | D | 05, 10, 31, 34 |
| [36](phases/PHASE_36.md) | Indirect PBR integration | D | 30, 31, 35 |
| [37](phases/PHASE_37.md) | Linear HDR scene color and resolve | D | 31, 34, 36 |
| [38](phases/PHASE_38.md) | Energy-aware multiresolution Bloom | D | 37 |
| [39](phases/PHASE_39.md) | Manual exposure, tone mapping and final display | D | 16, 37, 38 |
| [40](phases/PHASE_40.md) | Lighting/shadow default profile and D acceptance | D | 30, 31, 32, 33, 34, 35, 36, 37, 38, 39 |
| [41](phases/PHASE_41.md) | Complete Scene/ECS authoring and hierarchy lifecycle | E | 04, 14, 24, 40 |
| [42](phases/PHASE_42.md) | Project roots and persistent identity | E | 41 |
| [43](phases/PHASE_43.md) | Shared property metadata and structured authoring diagnostics | E | 09, 13, 29, 40, 41, 42 |
| [44](phases/PHASE_44.md) | Scene/component persistence and semantic round trip | E | 41, 42, 43 |
| [45](phases/PHASE_45.md) | Constraint and ragdoll persistence extensions | E | 26, 27, 28, 29, 44 |
| [46](phases/PHASE_46.md) | AssetDatabase, import settings and observable tasks | E | 12, 42, 43, 44 |
| [47](phases/PHASE_47.md) | Reimport dependencies, rollback and last-valid notifications | E | 11, 13, 46 |
| [48](phases/PHASE_48.md) | Authoritative transactions and grouped mutations | E | 13, 14, 41, 43, 44, 45, 47 |
| [49](phases/PHASE_49.md) | Undo/Redo and dirty/saved revisions | E | 44, 45, 48 |
| [50](phases/PHASE_50.md) | Edit/Play state and resource isolation | E | 13, 24, 41, 47, 48, 49 |
| [51](phases/PHASE_51.md) | Extend shared authoring/debug visualization and E exit | E | 18, 29, 40, 41, 43, 50 |
| [52](phases/PHASE_52.md) | RBS admission and qualification of original C scaling residual | F | 12, 40, 51 |
| [53](phases/PHASE_53.md) | Historical CPU lineage and causal attribution | F | 52 |
| [54](phases/PHASE_54.md) | Full RBS authoring-to-runtime integration | F | 12, 17, 29, 40, 51, 52, 53 |
| [55](phases/PHASE_55.md) | Core Editor handoff contract and final acceptance | F | 54 |

Each linked file supplies goal/findings, dependencies/entry/predecessor, bounded source locators/budget, non-goals/invariants, validation/reuse, observable exit, RBS/manual coverage and review/seal behavior. [VALIDATION_CONTRACTS](VALIDATION_CONTRACTS.md) C0/V0/M0/D0 is incorporated explicitly, so invariants and evidence rules are maintained once.

## Canonical findings and residual disposition

[The findings ledger](FINDINGS.md) is canonical for CQ/API/EVT/INP/CAM/TRN/DBG/PHY/LIT/AUT/INT findings. Baseline, evidence, impact, technical status, severity, priority, owner, gate, dependencies, work and verification are recorded per issue. [The five original performance records](PERFORMANCE_RESIDUALS.md) are the sole expanded records for PERF65-01, PERF65-02, PERF65-03, PERF65-04 and PERF65-05-06.

- CQ-01 blocks A START; CQ-02 is scoped fix-when-touched, with unrelated frozen legacy deferred.
- API-01–05 block A exit: narrow root/service/resource dependency direction, CPU geometry through MeshAsset/MeshHandle, one typed standard material/shader model, safe reload and caller simplicity.
- EVT-01/INP-01/CAM-01/TRN-01 block B exit; DBG-01's first provider is before C diagnostics.
- PHY-01 core stability blocks C exit and joint/ragdoll START until resolved or explicitly accepted with quantified bounds; PHY-02–05 cover API/collider/CPU/joint/ragdoll gates.
- LIT-01–05 block D exit: shared BRDF/units, required Spot shadow, IBL/HDR/Bloom/display and accepted quality/temporal/CPU/GPU/memory/tails. Existing Directional/Point shadow functionality does not by itself establish the new visual/performance default.
- AUT-01–05 block E exit; INT-01 blocks Core Editor handoff.
- Original residuals remain technically OPEN and historically ACCEPTED_OUT_OF_SCOPE. Proposed PRE_EDITOR owners: PERF65-03 Phase 12/A exit, PERF65-04 Phase 40/D exit, PERF65-01 Phase 52 and PERF65-02/PERF65-05-06 Phase 53/Core Editor handoff. These proposed new gates require owner review, not retrospective changes to Rendering approval.

No source finding establishes that the five performance issues need pre-A implementation. A new concrete entry blocker would require an explicit corrective-scope decision; it is not permission to silently add a remediation phase.

## Validation admission and unresolved decisions

All phase validation is proposed, not run. [The validation contract](VALIDATION_CONTRACTS.md) declares the minimum scenario families, exact manifest fields, source/build/input identity, reuse, measurement boundaries, sample/repeat/time bounds, stops and PASS/FAIL/INCONCLUSIVE. Unknown numeric tolerances/budgets are explicitly OPEN_DECISION; they block the affected START or gate.

Owner decisions before dependent execution include: activation/publication and output isolation (OD-01/02); A API/geometry/material/async criteria (03/04); input/camera tolerances and routing (05); current Physics reproduction/cause/rest/CPU/joint bounds (06–08); light units/color/exposure/tone/quality/temporal/cost and p99 protocol (09–11); staged original-performance diagnostic scope/criteria/protocol before START and comparability admission before comparative measurement (12); persistent/schema/transaction/isolation policy (13); final integrated human/gate acceptance (14).

For Phases 52–53, [P0](VALIDATION_CONTRACTS.md#performance-admission-p0) approves bounded adapter/diagnostic scope, equivalence/lineage criteria, measurement protocol, original applicable thresholds and execution limits before START. The phases then establish the adapter or lineage evidence; comparability must be verified and admitted before comparative performance measurement. Failure to establish it retains OPEN / INCONCLUSIVE and requires explicit owner disposition at the declared stop. Future OPEN_DECISION items remain due at their applicable entry or gate.

No convenient numeric threshold is invented. Existing applicable 16.667 ms, 5%, absolute historical CPU or input-specific Physics criteria keep their original meaning. New PBR/HDR/Bloom appearance uses controlled owner-reviewed references; it does not demand accidental pixel identity with the old pipeline. Failed, unstable or insufficient-tail evidence remains visible and cannot pass.

Physics diagnosis must quantify velocity/spin/drift/penetration/contacts/impulses/energy/sleep churn and CPU/pass/body/contact cost. D requires all four dimensions: correctness, visual quality, temporal stability, performance. The practical default is chosen from measured/accepted profiles; no silent removal of shadows/casters or quality reduction.

The source-based conclusions are complete for review even though future implementation acceptance numbers remain unresolved. Plan readiness is not execution readiness.

## Inactive governance and activation

[ACTIVATION_PROPOSAL](ACTIVATION_PROPOSAL.md) proposes pre-editor/refactor at C:/dev/GEngine-pre-editor from the full verified Phase 68 SHA, isolated generated projects/intermediates/binaries/libraries/runtime settings/caches/evidence, and allowlisted hash-preserving document transfer. The current audit stays in the Rendering worktree.

Owner acceptance of this plan precedes separate explicit activation/minimal-setup authorization. Phase 00 requires that authorization plus verified Phase 68 completion; it creates the isolated worktree/minimal workflow and stops for review. C0's already activated isolated-worktree entry condition applies from Phase 01 onward, not to Phase 00. This documentation revision grants no activation authorization. The proposal reuses checkpoint/snapshot/seal/protected-path/review/publication concepts, including the canonical RBS owner-mutable membership policy. No live AGENTS/skills/helper/format policy, approval state or publication policy is changed now. Proposed command names do not constitute currently supported routing.

Core Editor UI (selection/gizmos/Inspector/Content Browser), topology editing, Area lights/shadows, optional motors/active ragdoll/animation blending/IK, auto exposure and speculative many-light/GI techniques remain intentionally deferred. Their absence is not a hidden omission from required chain/hinge/passive-ragdoll or Directional/Point/Spot-shadow/Bloom scope.

## Readiness and smallest next action

- **Audit Completion: COMPLETE_FOR_REVIEW.** Every required topic has a disposition in HANDOFF_AUDIT; evidence gaps constrain future acceptance, not the reported source/provenance conclusions.
- **Plan Readiness: READY_FOR_HUMAN_REVIEW.** One coherent proposed sequence, owner/gate mapping, earliest providers, bounded contracts and explicit validation decisions are present.
- **Milestone A Readiness: NOT_READY.** Setup and Code Quality prerequisites are only proposed.
- **Next Action:** owner review of this package; smallest decision is accept or revise the phase/dependency/gate plan. After acceptance, separately authorize the concrete isolated activation/minimal setup. No current PRE_EDITOR START command is invented.

[PROPOSAL_MANIFEST.json](PROPOSAL_MANIFEST.json) is the compact coverage/resume index. [evidence/package-files.json](evidence/package-files.json) lists actual proposal/audit artifacts and transfer hashes. [evidence/final-preservation.json](evidence/final-preservation.json) records final read-only checks against the entry observation. None is an approval receipt or seal.

Stop here. No activation, implementation, staging, commit, tag, push or next phase is part of this delivery.

