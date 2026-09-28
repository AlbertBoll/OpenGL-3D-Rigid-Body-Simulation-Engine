# Shared phase and validation contracts

**PROPOSED / NOT_STARTED.** Normative only if the owner accepts this proposal and separately authorizes activation. This file is incorporated by every proposed phase contract; it is not an active validation policy, approval receipt, or permission to run anything now. Master references: sections 1.1–1.2, 17, 18–18.2.

## Common phase contract C0

Each phase also has a small specific contract in phases/PHASE_XX.md. The approved predecessor is the immediately preceding numeric phase; Phase 00 enters from the verified full Phase 68 commit. Additional dependency edges name earlier providers only. Implementation detail is refined before START, never by silently expanding an active phase.

From Phase 01 onward, entry requires the accepted plan, already activated isolated worktree, completed predecessor publication, unchanged protected work/index, a declared candidate baseline, bounded exact allowlist, and resolution of OPEN_DECISION items due at that entry. Phase 00 is the bootstrap exception: it requires verified Rendering Phase 68 completion (approval, commit, tag, publication and completed workflow records), owner acceptance of the plan, and separate explicit activation/minimal-setup authorization. It creates the isolated worktree and minimal workflow setup, validates that setup and stops for review; an already activated PRE_EDITOR worktree is not its entry prerequisite. This documentation revision grants no activation authorization.

Decisions and evidence due at a later gate need not exist at phase entry. A diagnostic phase may begin under an approved bounded scope/protocol while producing the evidence its later gate requires; that does not pass the later gate. In particular, Phases 52–53 follow P0 below: approve criteria/protocol/limits before START, establish equivalence or lineage during the phase, and admit comparability before comparative performance measurement. Only an explicit applicable owner instruction starts a phase.

Default scope is 2–6 production files plus 1–3 focused test/build/documentation files, excluding compact mandatory workflow records. The phase specifies its expected subset. Listed existing paths are source locators, not an instruction to modify all of them; NEW design slots do not pretend files exist. Count shader/header/source pairs as files. RBS adoption counts as production. Before START, select exact members within budget from these locators; when a cohesive deliverable demonstrably needs more, request an explicit reviewed scope amendment or numeric split before implementation. No hidden cleanup. Preserve semantic/mechanical/API/correctness/performance separation.

All phases preserve these invariants:

- Approved C++23 gate, MSVC 14.44.35207 / VS2022 v143 / SDK 10.0.26100.0, Premake, dependency versions, Debug MTd / Release MT. No unrelated build-system/toolchain migration.
- No new explicit throw/try/catch or custom recoverable exceptions in GEngine-owned production. Use typed expected results, structured diagnostics and transactional cleanup; assertions for invariants only. Logging uses the engine abstraction; direct print is reserved for appropriate tools/bootstrap/tests.
- Normal consumers see semantic GEngine contracts, no OpenGL/GLAD/SDL/Assimp/ImGui backend types, native aliases/forward declarations or handles. RBS's approved ImGui widget use remains a consumer exception, not a backend leak. No PImpl or alternate-backend mandate.
- GPU/context/ImGui GL/upload/readback/destruction stay on the context thread. Workers perform CPU work. GPU owners move once, destroy once before teardown. Publish before extraction; retained exact versions survive submission; immutable RenderFrame copies no heavy assets.
- Reuse typed slot/generation/domain identity, existing MeshAsset/MeshHandle, material/shader/pipeline registries, scheduler and retirement. Persistent IDs are separate. ECS render components contain intent/handles.
- One fixed-step Physics scheduler. Authoring pose, previous/current simulation poses, presentation interpolation and RenderFrame pose have distinct ownership. Events do not drive Physics steps, render sequencing, explicit commands or resource publication.
- CPU geometry/collider descriptions are independent from native GPU state. Constraints/ragdoll use the existing body/solver architecture. D uses A's material semantics, one shared Direct PBR model and per-light visibility. IBL/emissive are not incorrectly shadowed by an unrelated light.
- E extends the earliest shared providers; it introduces no parallel event bus, debug renderer, property system, registry or general job scheduler.
- Concepts/requires constrain meaningful generic semantics; no new enable_if migration or ornamental template expansion. Keep layout/ABI static_asserts.
- All protected/unrelated changes remain intact. No force-add, reset/clean, force push or moving approved tags.

Review behavior for every phase: execution validates the exact candidate, records reuse and failures, seals compact identity/evidence including the full RBS canonical owner-mutable file, then stops for human review. The owner-mutable rule preserves later unstaged RBS content through exact sealed index publication; other drift is not excused. Approval consumes the verified seal and completed evidence without automatic rebuild/retest. Changed inputs or explicit revalidation follow the adopted workflow. Rejection selectively removes active phase changes only. Approval/publication routing is inactive until Phase 00 setup and explicit authorization; no phase may self-approve.

## Validation envelope V0

Future application-level runtime, visual, performance and human acceptance use **RigidBodySimulation only**. Other maintained apps receive affected-interface inspection and a compile/link check only if the changed shared boundary or an applicable mandatory gate requires it. Focused tests supplement concrete invariants; they do not replace RBS integration. Do not run Editor, Breakout, RayTracing, a substitute demo, historical standalone performance fixtures or a full old matrix by habit.

The old Rendering validation policy/Phase 68 contract required more applications and old performance suites; the Phase 68 owner amendment superseded those requirements for Phase 68. PRE_EDITOR Phase 00 must adopt its own explicit RBS-only rule while retaining applicable mandatory source/build gates. Any still-binding conflict is named and resolved by the owner before execution, not waived or expanded silently.

For each affected candidate:

1. Record approved baseline commit, candidate tree/diff fingerprint, exact source/input/shader hashes, build command/tool versions/CRT/configuration and executable/library hashes.
2. Inspect the affected normal boundary, owner/lifetime/error path and actual RBS caller. Compile the affected RBS dependency targets in Debug and Release when code/shared contracts change. A documentation-only phase does not build. A strictly mechanical phase uses lexical/diff verification and affected compilation; do not invent runtime work for unchanged behavior.
3. Run only the phase's focused correctness cases and applicable scenario variants. Log errors, cancellation/rollback, stale identities and shutdown when those paths change. Reuse matching unaffected accepted evidence; record its identity and why it applies.
4. Capture the declared metrics with explicit measurement boundaries and observer-overhead comparison. Missing GPU queries are missing, never zero. Requested bytes, committed bytes, process memory and measured VRAM are different measures.
5. Stop at sufficient evidence or a declared stop. No expansion until results become favorable. Keep every run/failure/inconclusive result.

Future evidence root is C:/dev/GEngine-pre-editor/logs/pre_editor/phase-XX/ (proposed; not created). Each campaign contains scenario-manifest.json, identities.json, raw observations, summary.json, review notes and relevant images/sequence hashes. Separate mutable runtime settings/caches go under that worktree's runtime directory, never Phase 68 sealed locations.

## Mandatory scenario manifest M0

The following fields must be concrete and reviewed **before the affected START or evidence campaign**, even when a proposed preset is named below:

- Preset/version/input SHA-256, random seed or explicit deterministic object list, physical units and coordinate conventions.
- Baseline/candidate source and binary identities; same configuration and intended scope of comparison.
- CPU/RAM/GPU/driver/OS, power mode and external load assumptions; no assumption the historic laptop/driver remains current.
- Viewport physical pixels and logical/DPI dimensions, camera pose/projection/near/far, light transforms/color/intensity/range/cones/shadows, materials/textures/samplers/environment.
- Physics initial state/fixed step/pass/sleep/contact settings; quality profile including all shadow, IBL, HDR, resolve, Bloom, exposure and display settings.
- Pacing/VSync/frame cap, warm-up, sample count/runs, maximum repeats/total time, measurement start/end, query drain, expected observer overhead.
- Applicable metrics/units, absolute or relative budgets, image/temporal criteria, permitted intentional visual changes, human acceptance.
- Exact PASS/FAIL/INCONCLUSIVE rule, stop conditions and evidence reuse assumptions.

All preset names below are **proposals, not existing implemented switches/files**. Do not claim reproducibility from a name alone. Missing values are OPEN_DECISION under the decision register and block dependent execution. No broad Cartesian product: each phase selects the smallest named variants addressing its change.

## Proposed RBS scenarios and minimum evidence

| ID | Concrete behavior and focused variants | Measures / required decision |
| --- | --- | --- |
| RBS-A-Authoring | Create Cube/Plane/Quad/Grid and parametric Sphere/Cylinder/Cone/Capsule; Standard/Unlit/Masked/Transparent/Debug materials; assign, share, duplicate, MakeUnique, stretch and explicit Bake; valid/invalid custom shader and explicit reload; asynchronous image success/failure/cancel | Handle equality/new identity, bounds/normal/tangent correctness, unchanged old frame, revision/compile/upload/allocation counts, typed failure and consumer-call simplicity. Compare preserved paths to entry; intentional new primitives/templates use controlled references. OD-03/04 |
| RBS-B-InputCamera | Press/release within one frame, held/delta/wheel/modifiers/text; focus loss/regain and capture release; UI/view/game consumption; self-unsubscribe and nested dispatch; active Scene Camera versus Editor view; resize/minimize/DPI/pick ray | Dispatch order/count/lifetime, stale queue rejection, event-to-consumer latency, allocations/copies/backlog; world/view/NDC/pixel round trips and pick/render alignment. Floating-point/DPI tolerances and latency budgets OD-05 |
| RBS-C-Rest | Import the exact historical lattice object list/hash, verify against current source interpretation, plus sphere-ground/frictional rest/spin/low-speed impact/multi-contact and a small box stack | Linear/angular velocity, drift, penetration, normal/friction impulses, restitution contribution, contact churn, energy, active/sleeping bodies, wake churn, passes, per-stage CPU and transient bytes. OD-06/07 |
| RBS-C-Joints | Distance chain, anchored ball/socket, free and limited hinge, passive ragdoll drop/settle with capsule/box/sphere bodies, parent-child filters and bone mapping; wake/teardown/stale-body cases | Anchor/length/angular error, limits, energy, sleep/wake, contacts, solver cost versus bodies/joints/passes, typed identity cleanup; debug overlays must match bodies. OD-06/07/08 |
| RBS-D-Reference | One controlled scene with dielectric low/high roughness and metallic low/high roughness objects, normal mapped glossy tiled floor, large receiver, thin caster, near/far receivers; isolate each light then mixed Directional/Point/Spot, each shadow on/off; IBL rotation/replacement; emissive and direct/specular HDR highlights; Bloom off/on; dark/bright and exposure variants | Common BRDF/attenuation/cone/visibility, color-space handling, >1 HDR retention, IBL roughness response, energy-aware Bloom/tone/display. Record all light/material/camera/environment/quality identities. OD-09/10/11 |
| RBS-D-Motion | Reuse the reference scene with separately scripted camera crossing cascade splits, light motion, caster motion; point face seams and spot cone/frustum alignment; cached then dirty shadows | Contact/softness/aliasing/shimmer/acne/peter-panning/leaks/floating shadows, cascade seam transitions, point seams, thin/grazing surfaces, temporal coherence. Paired frames plus sequence/human review; no screenshot-only pass. OD-10/11 |
| RBS-D-Cost | The same accepted scene/profile at matched resolution/pacing, minimum representative one-factor changes to caster count, total/shadow light count, cascade count, point/spot shadow count, resolution/filter samples, cached/dirty state; one supported lower and one higher profile only where informative | Frame/preparation/submission CPU; delayed GPU per light/shadow/IBL/HDR-resolve/Bloom/tone; draw/instance/caster/state/upload counts, invalidations/cache hit, payload/VRAM where measurable; median/p95/p99 and query completeness. No unsupported extra light silently ignored. OD-10/11/12 |
| RBS-E-Authoring | Project/Scene create-open, shared schemas, typed errors, grouped property/entity/hierarchy edits, save/unload/load, identity restoration, Undo/Redo, import/reimport failure/success, task cancellation, Edit/Play/Stop | Semantic round trip; no absolute paths/runtime handles persisted; committed-only notifications; dirty/saved revision; old resource validity; no runtime changes to authored state/callbacks; malformed/version/missing inputs. OD-13 |
| RBS-F-Scaling | Candidate RBS admission of original N=8192 and Q=1/3724/7448/dirty variants with equivalent phase/cost boundaries, paired control and candidate | All PERF65-01 CPU/memory/repeatability gates; do not infer equivalence from matching counts. OD-12 |
| RBS-F-Integrated | The full operation sequence in Phase 54, including all three required shadows, passive ragdoll, persistence, Edit/Play, replacement and shutdown | API/dependency/code-quality, Debug/Release correctness, D human/temporal/cost acceptance, C stability/performance, all five original residual dispositions. OD-14 |

## Performance admission P0

Phases 52–53 separate entry authorization from evidence they are meant to produce:

1. **Before START:** approve the bounded RBS adapter/diagnostic scope, equivalence or lineage criteria, measurement protocol, original applicable thresholds, observer assumptions, sample/repeat requirements and total execution/stop limits. Define the evidence needed for comparability. Completed adapter equivalence or historical lineage proof is not a START prerequisite.
2. **During the phase:** establish the RBS adapter equivalence (52) or historical source/build/input/metric lineage (53) within that scope. Only admitted, bounded diagnostic/equivalence work is permitted at this stage; it cannot be presented as a qualified comparative performance result.
3. **Before comparative performance measurement:** verify the resulting evidence against the preapproved criteria and explicitly record comparability admission for the exact workload, identities and measurement boundaries. No paired budget/regression or attribution campaign proceeds on assumed equivalence.
4. **If comparability cannot be established:** retain the original residual technical status OPEN and the comparison INCONCLUSIVE, stop at the declared limit, and request explicit owner disposition. Do not weaken thresholds, substitute a convenient baseline or retry indefinitely. Any protocol change needs review before a new comparison; all failed/inconclusive evidence remains attached to the original ID.

OD-12 tracks these separate gates. Other future OPEN_DECISION items remain unresolved until their applicable entry or gate, without becoming an accidental prerequisite for this documentation revision.

## Lighting envelope D0

Every D contract selects relevant RBS-D-Reference/Motion/Cost variants before START and carries the same four acceptance dimensions: correctness, visual quality, temporal stability and performance. It declares what exists in that phase and what is legitimately completed by an earlier/later providing phase; a resource-only phase cannot claim a not-yet-implemented shading pass passed.

Reference identity includes camera/material/texture/environment/light data, viewport, HDR format/resolve, exposure/tone/display and explicit quality profile. Shadow profiles record supported resolutions, cascade count/splits, filter/sample count, bias/slope/normal bias, update/reuse policy and Point/Spot settings; IBL and Bloom profiles record map/mip/filter quality. Record visible benefit, intended workload and CPU/GPU/memory cost per admitted profile. Preserve accepted visuals under matched conditions and separately identify intentional new behavior.

For relevant passes record CPU preparation/submission and delayed GPU elapsed time, draw/instanced/caster/state/upload counts, cache hit/invalidation, requested resource bytes and separately measurable VRAM/residency. Include median/p95/p99 when relevant, enough complete samples and repeatability to support the claim, and query missingness. Record UI/present CPU separately; do not claim GPU UI attribution from it. OD-11 fixes all unknown budgets, temporal/image tolerances and sample sufficiency before affected implementation, not after viewing candidate results.

Human review explicitly checks contact attachment, softness/sharpness/aliasing, shimmer, acne/peter-panning/leaking/floating shadows, directional cascade seams/transitions, Point cube seams, Spot cone/frustum alignment, thin/grazing/near/far receivers, and independent camera/light/caster motion. Automated comparisons are complementary. A visible shadow or single image is insufficient.

The final accepted profile includes all three required shadow types, shared Direct PBR, IBL, normal mapping, HDR/emissive/highlights, physically based Bloom, manual exposure and tone/display. No quality/caster/shadow reduction to satisfy timing without explicit review; no extra resolution/samples without measuring cost. Area Lights/shadows, Forward+/clustered/tiled lighting, GI/DDGI/volumetrics/ray tracing and auto exposure are deferred, not hidden requirements.


## Bounded campaigns and classification

The following are proposed execution bounds to be accepted before use, not invented acceptance limits.

- Functional cases: one deterministic pass per affected Debug/Release configuration. Retry only for a documented external interruption under a reviewed amendment; do not count an interrupted run as PASS.
- Historical CPU profile where compatible: 120 warm-up frames, 240 measured frames, three fresh-process runs per control/candidate; maximum one complete batch, zero automatic retries. This reuses an existing bounded protocol, but does not make 240 samples sufficient for p99. Total wall-clock cap and GPU-drain timeout are OD-02 before START.
- D timing: predeclare a sample count sufficient for its p95/p99 claims and query completion, in OD-11, before measuring the candidate. Do not adopt the old 240-frame count for p99 by convenience. One approved batch, no automatic repetition; record run spread and all outliers. Repeatability criterion is also OD-11; old 5%/10% rules apply only to their actual historical campaigns.
- C diagnostic: first reproduce the historical 15-second one-pass 120 Hz lattice case in RBS, with the exact saved initial list and sleep configuration; compare at 60 Hz or another pass count only to answer a named causal question. C qualification may reuse the historic 120-second settling interval and 110–120 s tail, plus the existing 20-second stack protocol where input parity is shown. Do not automatically rerun all eight old 1/2/4/8-pass combinations. OD-06 declares the small required set and total time before START.
- Joint/ragdoll campaign durations, scene sizes and numeric error/energy/cost limits are OD-08, informed by admitted C baselines. These are required C exits, not optional features.
- Performance runs use Release MT with profilers/overlays controlled. Debug is functional evidence. Old nominal Release Physics data built against MTd is diagnostic provenance, not a Release MT throughput baseline.

PASS requires matching identities, every applicable quantitative gate, complete sufficient samples, invariants and explicit human review where required. FAIL means a violated declared criterion/invariant. INCONCLUSIVE means missing identity, unstable repeats, insufficient tails/samples, unavailable instrumentation or unresolved comparability; it cannot close a finding. A diagnostic may finish with a valid causal report and unresolved technical issue, but dependent remedy/joint gates stay closed.

Stop immediately for a crash, assertion/invariant violation, GL error/context-thread failure, memory corruption, unbounded resource growth, publication/identity conflict, missing required metrics, or the predeclared time/repeat cap. For an intentionally invalid-input case, the expected typed error with unchanged prior state is success, not a campaign abort. Store evidence before any future revision. Do not move a threshold after seeing candidate data.

## Numeric evidence reuse and owner decisions

| Decision | Missing choice/evidence and affected entry |
| --- | --- |
| OD-01 | Accept plan, proposed gate assignments, no pre-A perf phase, and separate activation destination/scope. Blocks 00 |
| OD-02 | Isolated build/runtime/log paths, exact campaign wall-time/query-drain caps, hardware identity, existing mandatory-gate conflicts. Blocks any affected execution |
| OD-03 | Review A API shape, material/alpha/texture semantics, geometry parameter bounds/negative/zero-scale/Bake policy; exact RBS reference inputs. Blocks 04 and dependent affected A feature STARTs |
| OD-04 | Admit async workload/boundary and original 16.667 ms stall target applicability; payload limit; outcome of diagnostic versus bounded implementation amendment. Blocks 12 and A exit |
| OD-05 | Event dispatch/routing/thread/overflow policy; camera coordinate/DPI tolerances and input latency budget from matched baseline or owner decision. Blocks 13–17 as applicable |
| OD-06 | Matched current RBS Physics reproduction, causal model admission, rest tolerances/impact preservation and exact timestep/sleep cases. Blocks 19 protocol, 20 remedy and 21 qualification; 26–29 wait for resolved or explicitly quantified accepted stability |
| OD-07 | Physics stage/total CPU, tail/scaling/allocation limits from matching baseline or owner decision; no post-candidate retargeting. Blocks affected C performance gates/25 |
| OD-08 | Chain/ball/hinge/limit/ragdoll scene definitions, duration, error/energy/settling/cost limits and permitted joint features; no motors/active ragdoll by implication. Blocks 26–29 |
| OD-09 | Coherent light intensity/range/cone units, current capacity/overflow, shared BRDF/normal/alpha contract and material migration; environment orientation/encoding. Blocks 30–36 as applicable |
| OD-10 | HDR format, manual exposure representation/order, tone mapper/display contract, Bloom energy/artistic controls; controlled visual reference with accepted intentional changes. Blocks 37–39 (and earlier affected color/light semantics) |
| OD-11 | D quality profiles, baseline/new visual and dynamic tolerances, CPU/GPU/memory budgets, p95/p99 sample/repeatability protocol, practical default and human acceptance. Blocks each affected D phase's START/acceptance; 40 cannot invent them afterwards |
| OD-12 | P0: before START 52/53, approve bounded scope, equivalence/lineage criteria, protocol, original applicable thresholds and execution limits. Establish evidence during the phase; verify and admit comparability before comparative performance measurement. Failure to establish it retains OPEN / INCONCLUSIVE and requires explicit owner disposition; no threshold weakening or indefinite retries |
| OD-13 | Persistent ID/URI/schema migration/missing-data behavior, transaction resource ownership and Edit/Play copy-on-write policy. Blocks affected E STARTs |
| OD-14 | All A–E exits and residual closure/explicit acceptance, integrated scenario identities and final human acceptance authority. Blocks 54–55 completion |

Only matching historical constraints are reusable automatically as historical criteria. For example: the original C scaling 16.667 ms/5% limits, original render/work absolute limits, and original async stall limit remain unchanged. Historic Physics lattice bounds (171 of 180 quiet/sleeping in the final ten seconds, penetration at most 0.035, peak energy at most 30,391.2 against initial 30,240) apply only to that admitted input/protocol, not arbitrary chain/ragdoll or different material scenes. Historical stack penetrations are observations, not newly approved universal limits.

Preservation visuals use matched existing conditions where applicable. Intentional PBR/IBL/Spot-shadow/HDR/Bloom changes have controlled proposed references and owner visual acceptance; accidental equality with the incomplete old pipeline is not the criterion. Automated image checks and human temporal review complement one another.


