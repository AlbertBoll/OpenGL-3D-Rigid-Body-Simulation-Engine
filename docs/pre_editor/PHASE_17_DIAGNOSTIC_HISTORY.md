# Phase 17 diagnostic history and closure record

This record was prepared under the owner's **APPROVE PRE_EDITOR PHASE 17 — DIAGNOSTIC ARCHIVE AND FINAL CLOSURE** directive. It preserves the campaign's results; it does not grant acceptance or change a failed result. Phase 17 remains **NOT READY / APPROVAL BLOCKED** because Candidate26's mandatory Release qualification is incomplete.

The owner reports successful Release RigidBodySimulation human validation. That report is accepted as human feedback. The historical running executable's path, PID and SHA-256 were not established, and neither the earlier visual FAIL nor the later human PASS is retrospectively assigned to Candidate20, Candidate21 or Candidate26. All 13 current production files match the recorded post-human source and Candidate26. No production or check file changed while preparing this archive.

The companion [diagnostic index](evidence/phase-17-diagnostic-index.json) records full SHA-256/size identities, candidate build/run outcomes, the 13-production/four-check source map, selected diagnostic records, governance references and unchanged accounting. The [production diff](evidence/phase-17-production-changes.patch) records the exact Git content delta from approved Phase16 commit `1695884434323f68238e6894e200758754731f5b`. Raw-byte source hashes are separate from Git's line-ending-normalized diff. The [approval disposition](evidence/phase-17-approval-disposition.json) records why the guarded publication transaction was not entered. Original logs, frozen manifests, failed attempts and proposed/accepted decision records remain unchanged.

## Final evidence reconciliation

| Candidate26 gate | Actual result and limit of proof |
| --- | --- |
| Strict prequalification | Focused33 PASS after correcting exactly two expected checker metadata differences. Actual-TU compilation from Focused31 reused by exact identity; source/dependency/workload/Control05 and desktop preflight completed. |
| Debug build | PASS. Fresh ordinary and qualifying RBS; unchanged glad/GEngine libraries and compiler PDBs reused only with recorded source, dependency and option proof. |
| Debug qualification | **Full PASS**, process exit 0, 35.437 s; formal `audit-Debug.json` PASS. |
| Release build | PASS. Fresh glad, GEngine, ordinary RBS and qualifying RBS closure, 344.953 s. |
| Release camera/GPU/lifecycle/gestures | Completed checks PASS: 62 lifecycle checks, 12 camera/GPU segments, 96 frames, 288 direct GPU readbacks, all 65 gestures, Case55/59 and 20 held-Alt Orbits. This is partial coverage of a failed process. |
| Release performance | Both original Editor/Scene measured batches completed and their preserved samples satisfy the numerical and allocation/copy gates. They do not make the full qualifier PASS. |
| Release visual observation | **2048 stored rows PASS**, then observer capacity overflow before the required five-second interval completed. The failed call's geometry was not evaluated or stored. |
| Release five-second lifetime | **INCOMPLETE:** 1721 lifetime frames, 4.1387305 s wall time, activation age 4.162548902386334 s. |
| Release original 42-step UI sequence | **NOT COMPLETED / NOT REACHED**, because it follows the lifetime observation. The 65-case gesture/UI matrix is a separate workload. |
| Release overall | **FAIL**, exit 1 after 11.750 s. No full Release PASS audit exists. |
| Owner human validation | **OWNER-REPORTED PASS**, including focus, border, flash, controls, GPU picking and UI. Historical executable identity remains unverified. |
| Approval/publication | **BLOCKED.** No seal, finish, commit, approved tag, push or publication was executed. |

Primary evidence: [Debug audit](../../logs/pre_editor/phase-17/candidate-26/audit-Debug.json), [Release run](../../logs/pre_editor/phase-17/candidate-26/run-Release.json), [final causal analysis](../../logs/pre_editor/phase-17/revision-08/completion-01/stop-01/failure-analysis.json), [performance observations](../../logs/pre_editor/phase-17/revision-08/completion-01/stop-01/performance-observations.json), [immutable final handoff](../../logs/pre_editor/phase-17/revision-08/completion-01/stop-01/HANDOFF.md).

The tested Candidate26 Debug executable is SHA-256 `444e1e701ad12ccd86b1a10961de4578d7613d3a8488f303a95d3d3b71a5a313`; Release is `fbd5fde3243b5ff444d8d4242a061e1f1c2e658ab6329cb34d30fcaa247e5548`. These are the automated run identities recorded at execution, not an attribution of the owner's historical process. The frozen scenario manifest is `be819fa35658143e7bb1354460d4ea16183d4937173950d320236d65df3c964e`.

## Classification and evidence rules

**Proven production defect** means a concrete source-level causal path violated an accepted behavior contract; it does not establish that every untraced historical incident followed that path. **Proven validation defect** means a fixture, observer, build invocation or evidence assumption was mechanically wrong. **Environmental failure** identifies observed native/window/storage conditions. **Unresolved** means the preserved observations cannot distinguish the remaining causes. A successful diagnostic or later candidate never changes a preceding FAIL.

Short-circuit predicates retain their original evaluation order. A skipped operand is unknown, not false or true. Native foreground, keyboard focus, authoritative Viewport activation, pointer eligibility and visibility at a particular screen point are different observations. Periodic external-process absence samples do not prove continuous absence between samples. Source correctness, compilation, raw partial runtime success, formal acceptance and human feedback remain distinct.

## Candidate chronology

The index pins each available frozen manifest, actual build result and native run. A candidate number is not a launch count. Controls, diagnostic launches, build-only candidates and prelaunch failures are accounted separately.

| Candidate | Preserved outcome | Diagnosis / action |
| --- | --- | --- |
| 01 | Debug build PASS; no native candidate run | Before runtime, fix minimize observer's inactive CSV path and separate accepted world-pose tolerance from coordinate conversion input. Correction04. |
| 02 | Debug FAIL before first camera frame | Two diagnostic foreground guards attempted independent ownership of the same process foreground lock. Actual native foreground/focus checks passed before the second lock call failed. Corrections05/06 transfer the lease once. |
| 03 | Debug build FAIL; no launch | Disk exhausted during linking; bounded process cleanup and byte-preserving storage recovery. Correction07. |
| 04 | Debug FAIL at first observed Focus gate | Lease transfer succeeded. Combined native/engine predicate failed without individual operands. **Historical cause unresolved.** Correction08 preserves that uncertainty. |
| 05 | Debug build FAIL; no launch | Linker in-page error with underlying disk-full status. Correction10. |
| 06 | Debug build PASS; staging failed; no launch | Asset copy ran out of space. Correction11 admits only identity-verified asset/product reuse and fresh staging verification. |
| 07 | Debug diagnostic completed | Focus failure did not reproduce. No acceptance credit and no causal repair demonstrated. Correction12. |
| 08 | Debug diagnostic FAIL | First camera gate passed; second failed actual foreground equality with a 360 Huabao window. External interference proved for C08; C04 attribution unresolved. Correction13. |
| 09 | Debug FAIL at Segment8; Release build PASS | Combined projection/ray/pixel/interior/native-visibility condition failed. Its actual failing operand was not retained. No Release runtime acceptance. |
| 10 | Debug build PASS; prelaunch blocked | Isolation empty-inventory/permission proof could not establish the prerequisite. No native RBS launch. Corrections15/16 preserve all failed proof attempts. |
| 11 | Debug diagnostic FAIL at Segment8 | All projection/ray/pixel/interior operands passed; exact 360 Huabao hit-test hierarchy obstructed target0. Suspension/restoration passed but retained the obstructing window. No acceptance. |
| 12 | Debug build record FAIL; no launch | All compiler/link exits were zero, but reused Candidate11 compiler PDBs lacked pre-build identity. C13 used a fresh full-library build; the old failure was not backfilled. |
| 13 | Full Debug PASS; Release FAIL | First routed GPU pick failed. Original performance data preserved. **Historical Release cause unresolved.** |
| 14 | Release diagnostic completed | First-pick failure did not reproduce. Observer changes and missing C13 event data prevent historical attribution; no acceptance credit. |
| 15 | Debug build FAIL; no launch | New gesture fixture passed ImVec2 to a glm::vec2 helper (C2664). Mechanical correction preserved stimuli. |
| 16 | Debug build PASS; runtime FAIL Case55 | Expected target equality failed after camera gestures. Stale snapshot premise demonstrated in fixture; old wrong-hit cause not fully established. |
| 17 | Debug qualifying compilation FAIL; no launch | CameraSnapshot types were incorrectly qualified through Camera. Subsequent narrow `GEngine::` correction exposed C2872 ambiguity and an unsuitable selective-build invocation. All attempts retained. |
| 18 | Actual-TU compile and Debug build PASS; runtime FAIL Case55 | Snapshot/pose/viewport premise passed; frozen geometry proves the camera was below the ground slab and `wood_plane` obstructed the target. |
| 19 | Corrected visible-pose fixture frozen and actual-TU compile PASS; no runtime | Preserves Case54 movement, publishes one stable snapshot on an existing frame, then clicks on next existing tick. Qualification stopped for count limits. |
| 20 | Debug build PASS; early visual-oracle FAIL | Combined raw geometry/clip failure lacked individual runtime operands. CPU observer probe failed link before execution. Historical exact operand remains unknown. |
| 21 | Ordinary Release inspection build PASS; no formal native qualifier | Explicit neutral/focused/flash colors, widths, opacity and clipping implemented. Stored under `owner-release-21`, not a fabricated candidate21 runtime directory. |
| 22 | Debug build record FAIL; no launch | Compiler/linker completed, but consumed reused compiler PDBs lacked pre-build identity in evidence. Correction17 pins those dependencies; no backdated PASS. |
| 23 | Debug build PASS; runtime FAIL Case59 | `textActive && click(center)` failed with unrecorded suboperands. Owner human PASS retained without executable attribution. |
| 24 | Debug diagnostic FAIL, complete bounded trace | `textActive` false; all center-click operations skipped. Legitimate ImGui Alt-release navigation cleared ActiveId; the inherited fixture premise was invalid. |
| 25 | Corrected fixture compiled; Debug build PASS; prelaunch FAIL | `OpenProcess(0x101400)` error5 for foreground PID22720 prevented identity/eligibility proof. No RBS process launched. |
| 26 | Focused31 PASS; Focused32 metadata FAIL; corrected Focused33 PASS; full Debug PASS; Release FAIL | Least-privilege observer correction and exact C25 fixture retained. Final Release visual storage exhausted at 2048 rows before five seconds; subsequent UI not reached. |

Controls remain separate. Control01 failed fixture namespace compilation. Control02 built but generated-output/startup membership guards needed correction; Control03 supplied the initial matched run. A later comparability audit found missing consumed-input evidence, so it did not backfill trust into Control03. Fresh Control04 failed because its shadow source omitted the generated RenderDoc include used by `FrameSubmission.cpp`. Control05 copied that exact pinned input, built and ran successfully, and became the matched baseline. Only Control05 evidence was admitted for Candidate26 by strict preflight. Control03 and Control05 native processes remain included in cumulative accounting.

## Proven production defects and coherent corrections

### Scene and Editor camera lifetime

Phase17 separates authored Scene Cameras from transient Editor views using the existing entity generation/domain identity and a private transient marker. It does not fabricate handles or add a registry. Scene/Preview roles read authored camera transforms; Editor controls own their independent controller state. Active/preview switching, deletion/replacement, copy exclusion, retirement and stale-ID rejection after slot reuse are explicit checks. A frame samples one authoritative CameraSnapshot from the world Transform and viewport descriptor for rendering and GPU ID picking.

The revision01 source audit found additional concrete paths:

* Public camera-component attachment to a transient identity could make copy logic access absent authored ID data; raw compatibility attachment could make it appear authored. `_Scene.cpp` now excludes exactly the private transient marker from copied component/ID enumeration and rejects it in authored Scene Camera resolution.
* A plain projection component on a transient identity made strict view validation fail and also prevented retirement. Retirement now independently verifies current domain/generation and marker ownership, permits disposal of that plain camera value, and still rejects unrelated authored/resource contamination. It does not weaken normal view validation or generic ECS rules.
* Clearing EditorCamera angular damping during a role cancellation could change effective orientation and reconstruct a shifted arcball position on return. The RBS cancellation boundary preserves effective pose, folds pending angular displacement into the base angles and rebases the focal point while discarding motion. It does not use a stale snapshot from another role as cancellation state.
* A valid fallback hid its explanation while a recovered camera could retain stale unavailable text. Per-frame availability text and lifecycle/command messages were separated. A shortcut test previously checked only successful injection; it now verifies the independent expected FRONT pose.

Exact before/after ownership and changes: [camera correction01](../../logs/pre_editor/phase-17/revision-01/camera-correction-01-admission.json), [correction02](../../logs/pre_editor/phase-17/revision-01/camera-correction-02-admission.json), [correction03](../../logs/pre_editor/phase-17/revision-01/camera-correction-03-admission.json), their applied records, frozen manifests and the archived production diff. C26's 62 lifecycle checks passed in both actual runs; this does not supply the missing Release UI/lifetime gate.

### Alt ownership, Orbit and exclusive GPU picking

The [camera-input audit](../../logs/pre_editor/phase-17/revision-02/AUDIT_AND_AMENDMENT.md) established four source defects independently of Candidate13:

1. RBS treated every View LMB pressed edge as selection eligibility without retaining event-time camera intent. An admitted Alt+LMB could both Orbit and issue a GPU selection readback.
2. The routed pointer event carried physical event-time LeftAlt, but EditorCamera required LeftAlt to be held in the View keyboard state. UI could legitimately own the earlier Alt key while the later pointer press belonged to View, causing Orbit rejection while picking remained eligible.
3. A complete DOWN/MOTION/UP in one frame retained provider edges/motion, but the camera required the button still held at Update and discarded completed drag motion.
4. Sticky cancellation could suppress a later fresh eligible gesture in the same frame rather than clearing only the pre-cancellation work.

The accepted correction adds `EditorCamera::OnViewInput` for already-routed View events, bounded ordered intent/motion, and an RBS event-time plain-pick request. Pose changes remain in camera Update. Normal release retains completed movement and eligible plain-click intent; cancellation clears old intent; a later fresh press is considered on its own merits. LeftAlt+LMB remains camera-exclusive even if camera admission/capture fails or the Scene/Preview role prevents camera control. No CPU ray selector replaces GPU integer IDs. RbsFrame consumes the plain request once through the existing snapshot/coordinate/readback path.

Mouse-first compatibility, RightAlt policy, button ownership, UI priority, cancellation, out-of-bounds starts and owned drags crossing boundaries are retained. The current gesture matrix independently measures movement, readback count, identity, capture and activation; unchanged selection alone is insufficient. C26's 20 consecutive held-Alt Orbit cases and all 65 gesture cases passed in both Debug and the partially completed Release run, including exact plain GPU picking afterward.

### SDL polling and cursor restoration

The [native-poll proposal](../../logs/pre_editor/phase-17/revision-02/native-poll-scope-01/PROPOSAL.md) identified a separate deterministic production path: the pinned SDL contract says `SDL_SetRelativeMouseMode` flushes pending motion. InputManager called it synchronously during event dispatch and also flushed motion/cleared deltas on exit. Entering or leaving capture while EventManager was still draining the native queue could erase later motion, including a fresh gesture after release or focus loss/regain. Delaying logical capture until Update would instead misroute same-poll boundary crossings.

The accepted three-file provider amendment preserves immediate logical ownership, event consumption, gesture admission and cancellation. Only native relative-mode/grab changes are deferred inside an explicit native-poll transaction. EventManager uses an exit guard to commit when polling finishes; direct calls outside polling retain immediate semantics. Superseded native entries/grab writes are coalesced safely, typed failures fail closed, and restoration sequence bookkeeping distinguishes cancellation cleanup from later real input. The fixture covers nested/unbalanced polling, direct capture/FocusLost, same-poll transitions, boundary motion, loss/regain, native failure cleanup and cursor restoration. Intentional error-injection messages in these passing checks are not silently deleted or counted as unrelated successful production operations.

This correction explains demonstrated source paths, not Candidate13's missing historical event stream. See [implementation admission](../../logs/pre_editor/phase-17/revision-02/native-poll-implementation-01/admission.json) and [completion/refinement](../../logs/pre_editor/phase-17/revision-02/native-poll-implementation-02/admission.json).

### Authoritative Click-to-Focus and visible focus

Authoritative `m_ViewportActive` is distinct from hover, ImGui ActiveId/window focus and gesture ownership. Hover never activates. A click into an eligible inactive viewport activates it and consumes its press/release; a fresh press is needed for camera movement or picking, even when LeftAlt remains held. Completion of Orbit/Pan/Dolly/Flycam, button/capture release and cursor exit retain activation. Legitimate UI transfer, hidden/unavailable view or native focus loss may deactivate/suspend it. New gestures need pointer eligibility; owned drags may cross boundaries. Scene/Preview controls do not edit authored camera transforms.

The final integrated audit found that accepted policy alone had not implemented the complete persistent visual behavior. The later border audit proved a source-level contract mismatch: Candidate20's `HeaderActive` was dark gray, with base alpha0.45/width1 after the flash and peak alpha0.8/width2, and no specified inactive border. Its fade did **not** clear authoritative focus. The unidentified human run cannot prove whether that process lost focus, but the source independently failed the requested visual contract.

`RbsUi.cpp` now emits an opaque base independent of animation: inactive 1px `#475569`, focused 2px `#38BDF8`, plus separate 4px `#7DD3FC` emphasis with smoothstep alpha decay over 250ms. The animation ends at the persistent focused base, never at inactive appearance. Only a genuine inactive-to-active transition advances activation identity/start time.

Four filled DrawList strips are placed inside actual image/inner-window clipping bounds, snapped inward using the renderer's framebuffer transform and viewport DPI. Drawing occurs after image/text and completed routing on the viewport's own DrawList. Packed colors avoid Style.Alpha attenuation; filled strips avoid centered-stroke half-width clipping and anti-alias fringe ambiguity. The implementation adds no GL pass, framebuffer, shader, worker or persistent GPU resource, and does not force UI focus. Maximum emitted border geometry is 32 vertices/48 indices during emphasis and 16/24 otherwise.

Evidence: [integrated correction](../../logs/pre_editor/phase-17/revision-03/integrated-01/admission.json), [border source audit](../../logs/pre_editor/phase-17/revision-04/border-01/source-audit.json), [inspection-build handoff](../../logs/pre_editor/phase-17/revision-04/border-01/HANDOFF.md). C26 Debug formally passed strict geometry/opacity/color/width/texture/index/command-clip checks and the five-second lifetime. Release passed every stored geometry row but did not complete that lifetime. No claim of automated final-pixel inspection of the unknown human executable is made.

## Proven fixture/environment causes and unresolved failures

### Focus leases, native interference and Segment8

C02's [diagnosis](../../logs/pre_editor/phase-17/corrections/05/diagnosis.json) localized failure after native handle, visibility, foreground and thread focus checks had passed: a second diagnostic `LockSetForegroundWindow` acquisition failed. Correction06 transferred/released the first fixture lease before camera acquisition. It did not force focus or change production input cancellation.

C04's lease transfer logged success, but after stable extent frames the expression `!AcquireNativeForeground() || !InputManager::Focused()` failed without recording the individual native/engine operands. Zero camera frames were accepted. C07 added passive tracing and completed its diagnostic checks, but used an already-foreground activation branch and changed observation/timing. It did not reproduce C04. Neither C02's cause nor C07's success explains C04.

C08's preallocated trace preserved C04's conditional caption activation. The first observed camera gate passed. Between sampled self and foreign foreground states, records576–578 bound a change to about4.9722ms; the next gate failed specifically native foreground equality. Native handle/visibility passed; later GetFocus/lock/engine operands were skipped. The foreign HWND/PID matched archived `360huabao.exe`. The initiating actor/API and why the lock ceased to protect foreground were not recorded. No corresponding delivered foreign WinEvent/SDL loss notification before freeze is invented. C08's second-frame failure cannot explain C04's unobserved first-frame cause. See [correction13 diagnosis](../../logs/pre_editor/phase-17/corrections/13/diagnosis.json).

The owner explicitly allowed temporary, identity-verified isolation with independent restoration and a one-time exception to the **historical root-cause prerequisite**, while retaining both full qualification gates. Suspension did not remove existing external windows. C09 later failed Segment8's combined oracle, but its suboperands were absent; correlation with resize was insufficient to prove stale viewport or occlusion.

C11's failure-path observer preserved each original predicate once. At update91, Segment8 sample1, target0 `Phase08 Source`, projection and ray existed, max pixel error was about0.000010899px (limit0.05), interior margin55.20016px (minimum4), and native visibility alone failed. Native client960x640, ImGui window560x580, actual content560x556, snapshot/picking extents and conversion agreed. Logical sample(507.7115,485.6071) became native client(507,485), screen(720,605). `WindowFromPoint` returned child526054/root2490498 of PID20904 (`360huabao.exe`), rather than RBS HWND2950902/PID16420. The pinned target's suspension/restoration passed. This proves an external hit-test obstruction for C11; it does not prove C09's missing operand, a resize-induced z-order mechanism or later unevaluated target1 assertions. See [C11 diagnosis](../../logs/pre_editor/phase-17/revision-01/candidate-11-diagnosis.json).

The subsequent [SDL resize causal addendum](../../logs/pre_editor/phase-17/revision-01/candidate-11-sdl-resize-causal-addendum.json) separately proved a fixture ordering defect: pinned SDL revision `b424665e0899769b200231ba943353a5fee1b6b6` requests `HWND_NOTOPMOST` during windowed resize without `SDL_WINDOW_ALWAYS_ON_TOP`, after the fixture established native-only topmost exposure. The accepted correction re-establishes that exposure once on the exact owned RBS HWND after each existing Segment8/10 resize, with `SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE`; it preserves restoration and fails on API error. It is transition setup, not a failed-visibility retry or external-window change. The historical backend return/style change was not captured, so source proof of the demotion request is not proof that it exclusively caused C11's hit. [Fixture correction admission](../../logs/pre_editor/phase-17/revision-01/camera-fixture-correction-01-admission.json).

The owner subsequently closed Huabao normally. Later admission evolved to explicit process absence and desktop eligibility checks before each launch, with independent empty-inventory monitoring. No forced external termination or permanent system setting was used. C23's historical lease nevertheless observed two Huabao targets after an owner closure report; it restored them and did not prove any link to Case59. C24 and C26 prelaunch absence proofs passed. Periodic monitoring's sampling limits remain disclosed.

### Candidate13/14: the historical Release routed-pick failure stays unresolved

C13 failed the expected plain-LMB routed pick at Segment0/frame4. It did not retain click payloads, route/ownership/cancellation state, capture history or individual conversion/hovered operands. The [source/binary differential audit](../../logs/pre_editor/phase-17/revision-01/final-release-picking-audit-01/AUDIT.md) found that C13/14 ordinary executables differ only in four timestamp bytes; executable code/application data, frozen providers, generated fixture main, layout, workload, assets, dependencies and toolchain match. Their qualifying fixture binaries differ because C14 includes the observer.

C14 completed the Release first-pick diagnostic without reproducing the failure. It had51 queued ImGui events just before injection and54 at assertion; ImGui's position(609,471) differed from the engine click(632,541), yet the GPU pick passed. That proves UI state lag in C14, not a C13 cause. Normal same-frame DOWN/UP retains the provider's pressed edge; legitimate ownership/cancellation and frame-boundary coordinate state can still affect eligibility. The missing C13 event stream cannot distinguish those possibilities. Clearing queues, changing selection coordinates or assuming the later Alt defect caused this failure would be speculative. Observer timing/code-layout effects also remain possible. Original C13 FAIL/performance and diagnostic-only C14 results are preserved without acceptance substitution.

### Isolation and foreground-observer permission failures

C10's prelaunch empty-inventory path assumed a nonempty isolation target set; the authorized empty path required real proof, not a no-op success. Corrections15/16 preserved a later `PermissionError` during initial inventory before any proof case or RBS launch. That old evidence lacked the exact API/access/thread/handle context and cannot establish a historical permission cause.

Revision01 added fail-closed native API/error/access/identity attribution and independent empty/nonempty fixtures. A fresh surrogate restoration proof then failed `DebugActiveProcessStop` with error5. Source inspection found manual closure of process/thread handles supplied by a debug event, conflicting with their native lifetime ownership. The helper was corrected to close only the debugger-owned image-file handle and preserve native process/thread lifetime; subsequent fresh proof records are separate. Early handle closure is a demonstrated ownership defect, but its causal connection to the old Stop/error5 and initial inventory PermissionError was not proved. No rights escalation, Job-policy alteration or weakened restoration check was inferred. See [isolation correction01](../../logs/pre_editor/phase-17/revision-01/isolation-correction-01/admission.json) and [correction02](../../logs/pre_editor/phase-17/revision-01/isolation-correction-02/admission.json).

C25 failed before launching RBS at foreground PID22720: `OpenProcess` requested `0x101400`, returned error5, and identity remained unknown. C26's runner-local `_PrelaunchNative` removes only unnecessary `PROCESS_QUERY_INFORMATION`: `0x101000 = SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION` supplies the existing PID/creation-time/path queries and zero-time liveness wait. Identity, liveness, desktop, process-absence and denied-access failures remain strict. The isolation debugger helper keeps its separate rights/ownership contract. Focused31 proved the correction and fail-closed paths on the current desktop; it did not recover PID22720's historical security descriptor or prove which requested right was denied. See [correction19](../../logs/pre_editor/phase-17/corrections/19/admission.json).

### Case55: matching snapshots are necessary, visibility is separate

C16 injected a plain target click using a snapshot from before the preceding camera movement had been published. The first correction lets an ordinary existing frame publish a fixed authoritative pose/viewport and injects the same AltUP/MOTION/LMB_DOWN/LMB_UP tuple on the next existing tick. The pose remains fixed through consumption; Case54 movement and original target/GPU/native visibility assertions remain intact. Compilation failures in that correction were mechanical and preserved, not runtime evidence.

C18 then passed all three independent Case55 premise rows: fixed camera, pose/snapshot/viewport match, ordering and native visibility. It still hit `wood_plane`. The frozen pose was approximately(-14.120066643,-0.959028721,29.494075775), below the static ground slab. The quantized pixel-center ray intersected ground at3.316703258 units and the expected target at21.318951376 units. This independently explains C18's actual depth-tested GPU result as a fixture occlusion, not a CPU-picking replacement or wrong GPU ID.

C19 changes only fixture setup: after Case54 passes, the existing camera-pose helper sets focal(0,7,6), pitch0.12, yaw0, distance28; the next existing frame publishes the stable snapshot. The same target, projection/native assertions and GPU identity oracle remain. C26's matching-pose rows and exact post-gesture GPU target passed in both runs. The first earlier gesture crossing the floor was not recorded; C18's proof does not retrospectively settle C16's entire wrong-hit cause or C13. See [C19 handoff](../../logs/pre_editor/phase-17/revision-02/mechanical-correction-01/budget-stop-01/HANDOFF.md) and its referenced geometric proof/minimal diff.

### Case59: legitimate Alt release invalidated the fixture premise

C23 reported only `textActive && click(center)` false. Missing evaluated/skipped flags prevent assigning its failing operand after the fact. C24 traced the same fixture with bounded passive observation, no forced focus, extra pump, artificial delay or queue clearing.

At frame306/ordinal28, InputText1841863769 was active; the field click(1054,114) lay inside(948,102)..(1130,126), with Alt held. At frame308/ordinal47 LMB had released, text remained active and Alt navigation toggle was pending. Pinned ImGui InputText sets `ActiveIdAllowOverlap = !io.MouseDown[0]`; on Alt release, ordinary ImGui navigation can apply its layer toggle and call `ClearActiveID`. At frame309/ordinal51, Alt was false and ActiveId became0. At update310/ordinal55, `textActive` was false; center-click/motion/down/up were all **SKIPPED**. Recorded foreground/focus/hit identities belonged to RBS PID6920/HWND1246446, with no traced native focus event. External applications are not the demonstrated cause.

The wrong transition was in the fixture's assumption: Cases57/58 legitimately exercised UI-owned Alt and did not guarantee text remained active afterward. C25 preserves their exact stimuli and original oracles, establishes Case59's prerequisite with **one** ordinary plain click on the text widget, processes its release through normal frames within the existing bound, and requires actual text ownership, released modifiers/buttons and empty queued input. It then executes the original center-click predicate once and retains its full observation ticks. No ImGui focus setter/menu suppression, repeated setup click or queue manipulation was introduced. The five-second focus observation was scheduled on ordinary frames after gestures and before the original UI sequence. C26 Case59 passed in both runs. [Correction18](../../logs/pre_editor/phase-17/corrections/18/analysis-and-admission.json) pins the exact raw trace and source chain. C23's missing historical operands remain missing.

### DrawList observer limitations and final capacity failure

C20 stopped at update9/Segment0 on a combined geometry/clip assertion. Its observer treated raw anti-aliased stroke vertices as if they were final scissored pixels; source inspection showed why that could differ. The exact failing runtime vertex/clip operand was not retained. A bounded CPU probe failed linking before execution, so it supplied no causal PASS. The later required filled-strip border changed the relevant geometry; no obsolete CPU probe result was invented or demanded as acceptance.

C21's revised observer retained strict raw bounds, command clipping, opaque palette, strip widths, texture/index coverage and individual failure-bit recording. Its actual guarded translation unit was subsequently compiled. C22 exposed missing PDB input identities; correction17 repaired evidence closure before C23. These are distinct from the border source correction and do not establish C20's historical exact failure.

C26's final Release failure is fully localized: `CameraLifecycleChecks.h:700` allocates `std::array<Row,2048>`; `ObserveViewportFocus` at line730 first executes `if (p.count==p.rows.size()) { p.overflow=true; return false; }`. RbsUi forwards false at lines244–247 as a generic border-oracle failure. The array accumulated one row per ordinary frame across the campaign, including the five-second interval. Release filled it before that interval completed.

All 2048 stored rows have `pass=1`, `failures=0`. Last stored update2052 had active=1, activation13, opaque alpha255, base border present, flash0, 16 vertices/24 indices, valid image and command clip. The next call failed the capacity predicate **before** geometry/color/clip evaluation; its update number and operands were not stored. The CSV's `overflow=1` is the final global flag repeated at shutdown, not an indication that each earlier frame overflowed. The lifetime record stopped at4.1387305s wall time, leaving the later 42-step UI workload unreached.

Debug used the identical header, stored1065 rows, and completed695 lifetime frames over5.0024464s (activation age5.103921504691243). Release throughput made a fixed row-count assumption inadequate for an unchanged duration requirement. This is a **proven validation observer capacity defect**, not evidence of lost production focus or invisible border. No storage correction or retry was executed after this FAIL.

### Mechanical build/evidence corrections

All failed compilations and checks remain failed in the index. Important examples are initial/global namespace ambiguity, CameraSnapshot's wrong `Camera::` qualification, the later need for `::GEngine::`, explicit ImVec2 component conversion, and MSBuild `SelectedFiles` not proving actual translation-unit selection. The runner's corrected single-file build path verifies evaluated and compiled membership before claiming a fixture compile.

Evidence plumbing corrections distinguish build outputs from behavior inputs, pin generated startup descriptors and shadow includes, and include reused compiler PDB identities actually consumed by the linker. Focused29's ad hoc global occurrence assertion counted two legitimate UI paths; its later correction retained semantic comparisons. Focused32 incorrectly demanded exact equality of candidate25/26 protocol metadata despite an authorized fixture destination change and cap30-to32 change. Correction20 substitutes exactly those two expected values in a copied comparison, retaining every other behavioral/dependency/Control05 field. It does not rewrite C26's historical manifest to the later cap36. Focused33 passed. Focused24's recorded `AssertionError` also remains a failed preflight attempt; no missing detailed operand is invented.

## Performance and preserved contracts

Candidate26's original Release raw files contain 1440 measured rows per role: three OFF/ON pairs, 120 warmup and240 measured iterations per lane,64 events each. Exact sample tuple membership,64 deliveries and empty native/backlog drains were checked in the preserved analysis. No additional performance process or favorable repetition was used. All ON allocation/copy counters stayed stable.

| Worst observed across admitted role/lane comparisons | Value | Unchanged limit |
| --- | --- | --- |
| Median delta versus matched Control05 | 0.0044 ms | 0.100 ms |
| p95 delta versus matched Control05 | 0.0058 ms | 0.250 ms |
| Maximum measured latency | 0.0293 ms | 16.667 ms |

The [full per-role/per-pair values](../../logs/pre_editor/phase-17/revision-08/completion-01/stop-01/performance-observations.json) and original CSV hashes are in the index. Original C13 performance remains independently preserved; it is not substituted for C26. Performance observations within limits are not a full Release acceptance result.

Phase15/16 routing, GPU integer-ID selection, one CameraSnapshot per frame, existing scene identities, SphereBoxStack, Physics scheduling and rendering quality contracts remain unchanged. The accepted workload retains62 lifecycle checks,12 segments/96 frames/288 direct readbacks,65 gesture cases,42 UI steps, controlled DPI/resize/minimize/restore, native-poll/direct-call semantics, capture/cancellation, teardown and all numeric thresholds. Partial Release progress does not prove the unexecuted remainder. Controlled DPI tests and DrawList geometry are not a claim of exhaustive physical-monitor or final-pixel inspection.

## Human source identity

The original visual FAIL was reported without a verified process identity. A canonical executable observed at that earlier audit hashed `925f21cc3474a04631a0a0dbf499820326690bb5698fa857da6c973c576d549a`; local logs/tlogs did not prove it was the inspected process. C21's prepared ordinary inspection binary was `bf81d27f6b87fb9a1442c2d9bd8810517b0e160ae1d30c48f9cc91e3cf2e7444`; preparing its launcher was not execution or acceptance. At the later recorded human PASS, observed canonical bytes were `a112457ad5d4c0b47e52ccf837ed08de184a34bb0baf3961c8e9463b0e08c1ba`, again without historical running-process attribution.

The [post-human record](../../logs/pre_editor/phase-17/revision-05/post-human-01/human-acceptance.json) is SHA-256 `6f261f317eafde683245cbff29dedbc6b27101b5af45ea84961a317a77558611`. Current canonical RBS source is `b3ac31167db276ddaf8b324eaf6e9d0577c3ca00681e05698d290b1a30fd7714`. All13 production hashes match that record, C26 and current bytes; all17 admitted source/check files match C26. The latest owner again explicitly reports Release human PASS. It is recorded separately in the disposition without inventing a binary identity or requiring an extra manual pass merely because derived output hashes differ. No material production change occurred after the recorded human source.

## Accounting and approval decision

No count or time budget was reset. This archive uses documentation/read-only evidence operations, with zero builds, focused qualification invocations, desktop probes, native processes or diagnostics.

| Budget | Used / cap |
| --- | --- |
| Cumulative native launches | **18 / 18** |
| Revision native launches | **12 / 12** |
| Cumulative / revision diagnostics | **5 / 5; 3 / 3** |
| Focused invocations | 33 / 36 |
| Focused aggregate time | 491.971312 / 600 s |
| Cumulative / revision native time | 407.672 / 1620 s; 275.983 / 1080 s |
| Cumulative / revision build time | 7583.233 / 12600 s; 5222.030 / 7200 s |
| Revision Debug / Release build time | 3291.499 / 3600 s; 1930.531 / 3600 s |
| Desktop probes | 3 / 3; 4.39 / 45 s |

The last recorded storage observation was30,286,917,632 free bytes; it is historical, not a new free-space measurement. [Accounting](../../logs/pre_editor/phase-17/revision-08/completion-01/stop-01/accounting.json) retains every attempt and the original individual/configuration/aggregate limits. Spare time or focused invocations do not grant another native launch.

The active07–55 fast workflow remains START/REVISE -> READY -> owner RBS acceptance -> explicit APPROVE, with internal guarded seal immediately followed by finish/commit/tag/push/publication verification. There is no standalone SEAL command or inherited Phase15/16 full-solution rebuild requirement. Older proposal wording and interim handoff wording remain immutable provenance, not the current workflow.

The [approval skill](../../.agents/skills/pre-editor-phase-approval/SKILL.md) requires a READY candidate and states **“Missing/incomplete READY coverage blocks”** and **“Never finish if validation or seal fails.”** The active [fast-approval governance](active/PHASE_07_PLUS_FAST_APPROVAL_REVIEW.md) likewise stops on failed checks. [Validation-infrastructure autonomy](active/VALIDATION_INFRASTRUCTURE_AUTONOMY_REVIEW.md) permits bounded START/REVISE corrections; it expressly does not waive gates or alter approval guards. [Final completion authority](active/PHASE_17_FINAL_COMPLETION_AUTHORITY.json) conditions approval on all mandatory automated Debug/Release gates passing.

The accepted isolation Amendment04 exception concerns unresolved **historical causality**, not an incomplete current Release gate; it still requires both qualifying runs independently PASS. No accepted provision was found authorizing owner disposition of the C26 observer failure in place of the uncompleted automated checks. The latest directive explicitly requires preserving this boundary if such a provision does not exist.

Therefore the approval route stops **before internal seal/finish**. Branch remains `pre-editor/refactor` at Phase16 commit `1695884434323f68238e6894e200758754731f5b`; no Phase17 receipt, approval journal or local approved tag exists at this audit. No remote publication claim is made, and no remote mutation was attempted. The archive is retained in the worktree, not represented as committed or published.

**Minimum compliant correction:** correct only the proven observer retention/capacity problem while retaining every-frame strict evaluation, failure evidence, unchanged five-second lifetime and complete42-step UI workload; compile/prove that fixture and perform the required affected Debug/Release qualification under renewed bounded native authority. All18 native slots are used. Reuse unaffected evidence only with identity/impact proof; do not relabel the old Release FAIL, count owner feedback as an unexecuted automated PASS, or use unchanged favorable retries. No such correction, budget amendment or new execution is performed by this archive/approval request.

## Lessons retained for later approved work

* Establish event-time intent and button ownership before camera/selection consumers diverge. Frame-end held state cannot represent a completed gesture or a UI-owned earlier modifier.
* Keep logical routing immediate while deferring only native operations that can mutate a queue being drained. Direct-call behavior and every poll exit need separate proof.
* Make focus authority explicit. Hover, gesture capture, keyboard ownership and animation lifetime are not substitutes for activation state.
* Render persistent state independently of transient emphasis. Verify actual palette, alpha, width, clip and final drawing order, not just a timer or authoritative boolean.
* A matching CameraSnapshot/projection and a native-visible pixel do not prove the expected entity is unoccluded in the 3D scene. Verify the fixture's depth/visibility premise independently while retaining GPU IDs as the selection oracle.
* Preserve short-circuit operand masks, exact identities and observation timing when a combined oracle fails. Never fill missing historical operands from a successful later run.
* UI event queues and legitimate Alt navigation can change ActiveId across ordinary frames. Establish test preconditions with ordinary user stimuli, not focus forcing or queue deletion.
* Suspending an interfering process does not remove its windows. Foreground ownership and point visibility are separate requirements; permission failures must remain fail-closed with exact API context.
* Budget diagnostic storage against the duration and configuration throughput it must observe. Debug frame counts do not bound Release frame counts. Overflow is incomplete evidence even when every stored row passed.
* Verify actual evaluated/compiled translation units, consumed generated inputs and reused compiler artifacts before freezing. Separate expected destination/budget metadata from behavior while comparing all other fields exactly.
* Preserve failures, diagnostic-only success, human reports and formal qualification as different evidence classes. A comprehensive archive records uncertainty; it cannot manufacture readiness.

Phase17 is **not approved or published**, and Phase18 has not started.
