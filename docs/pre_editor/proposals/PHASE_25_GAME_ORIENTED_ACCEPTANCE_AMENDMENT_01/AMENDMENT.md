# PRE_EDITOR PHASE25 — GAME-ORIENTED PERFORMANCE ACCEPTANCE AMENDMENT

**PROPOSED; NOT ACCEPTED OR ACTIVE. One complete Owner decision, one bounded completion plan.** This document implements the Owner's policy-design request only. Phase25 remains NOT READY. It does not complete or restart the failed Revision03 protocol.

## 1. Recommended decision and exact claim

Accept this amendment as the prospective Phase25 exit contract: the final integrated Physics implementation must satisfy the existing absolute CPU reservations, resolved sleep savings, exact allocation/ownership requirements and physical-correctness requirements on the declared hardware and finite RBS workloads. Every required process must pass independently. Cost conclusions concern the declared M/P instrumented measurement boundaries; ordinary integration is established separately, without claiming an independently measured uninstrumented cost. Complete the missing measurements and ordinary integration checks in section 8, then obtain the required Owner rebuild and inspection.

Explicitly accept the limited assurances in section 4: no same-product version-delta test, no certification of stable scaling ratios, and no bounded profiling-overhead claim as prerequisites to this finite absolute-cost conclusion. Their actual failed/incomplete outcomes remain visible. This is a substantive narrowing of acceptance policy, not an infrastructure repair or a claim that Revision03 passed.

Suggested single decision: **ACCEPT PRE_EDITOR PHASE 25 GAME-ORIENTED PERFORMANCE ACCEPTANCE AMENDMENT 01**, including its named claim limitations, reuse rules, five planned processes, bounded mechanical reserves, resource ceilings and mandatory human gate. Acceptance would authorize implementation and that execution plan without another START/REVISE or intermediate numeric-budget approval. It would not approve Phase25.

The policy is designed with the earlier results known. No new numeric performance threshold is fitted to them. All retained numeric ceilings and the sleep formula were established before those captures. Any final PASS belongs to this revised, explicitly narrower contract; the original 173 repeatability failures and Revision03's 97 headroom failures remain FAILED under their own contracts.

## 2. Baseline, identical products and admissible evidence

Baseline: `871e065c53cb7364d2e8091dfff7e0080db14869`, branch `pre-editor/refactor`, plus the preserved Phase25 overlay. Accepted Collider C04 Revision05 remains integrated. The design audit rechecked all 743 frozen source inputs and discovery membership, 931 actual compiled inputs, 96 runtime inputs, 68 products and 10 build records: no mismatch. Compiler/link options, linked libraries, consumed generated headers, assets, settings and workload descriptors are bound by the existing freeze and build records.

C and the planned V referenced exactly the same final product and input closure **within each mode**. M, P, A and ordinary O are different instrument configurations; they are not asserted identical to each other. No V process executed. Accordingly there is no distinct version whose regression the proposed six additional V CPU processes could measure. Identity establishes this absence of a version delta; it does not establish runtime timing or allocation equality. Fresh A2 and the remaining CPU repeats still have to execute.

| Product | SHA256 | Existing applicable evidence |
|---|---|---|
| Debug A | `d5b52026b7c27000a7f05583659e780ce7256beef17bf4f628b692c299b1f1ce` | D1, 21 rows / 3,732 steps |
| Release A | `413571cc3215ac7f430150bf23754de6a6e33d8bf617d889c949251c9561c811` | C-A/A1, 18,960 ordinals |
| Release M | `007a4fc06e11711ae2b18e62578670b9d8d1e57135b424dd1243e102a6034462` | M1 full suite |
| Release P | `707117de181f93574173ff3a5238289b9976af287c717eb9202a77c68f6f294a` | P1 and P2 full suites |
| Ordinary Release O | `8369469151c69af26e37c765688c69f141f4c4de8bd0b9ef4e3f682f2eff4438` | Build/flags only; I1 not run |

Source SHA256: PhysicsProfile.h `ad70ab97e6302d2d51d141578055a5d888c0321002717dcda254827841dfdff4`; PhysicsSystem.cpp `b7ccdb4e47966b0cced415c6c20b457fc27146418cf0b8e1fefca6eccf433c6b`; RbsFrame.cpp `7e4405faf790433aed12b442f56d324c7b17df2e280e9bc98ca023f29124c452`; PhysicsCostChecks.h `af5808e0fb79e393d2953927220ccb7120a1a00b5238cbe0f80bf1d89bc18148`; runner `6d4652d662499b4d2c3e7340e10aa30a2574d9026fee0979557829e7f9a05375`; canonical RBS `30c1bab2f38d5bbd55ac8ca5e11c7b83b014e2e53b5ff61aeafe21d9ed5ddc6b`.

The tested platform is the recorded Ryzen 9 5900HX / 16 logical processors, Windows 10 22H2 build 19045.6466, recorded AMD/NVIDIA laptop GPU drivers and actual GL context. Use the same hardware, OS/driver identities, AC power and recorded power scheme, full affinity/normal priority, Release MT, MSVC 14.44.35207, SDK 10.0.26100.0 and C++23. Preserve cap0/swap0, 1280x720 and all existing rendering/settings inputs. Registry identities are not thermal/frequency telemetry. The second collection session does not imply the same cache, scheduler or thermal state.

Reuse **all** valid post-Collider M1/P1/P2 observations, including their unfavorable values, full raw samples and original timestamps. No pre-Collider CPU number enters the revised decision. Reuse D1, A1, allocation coverage, ordinary token/flags proof and the ten matching builds for their exact existing claims. Recheck their relevant identities before execution. Runner-only policy adaptation requires a new evaluator freeze and an explicit proof that compiled products, stimuli, raw measurement boundaries and prior interpretations used for retained gates are unchanged. Recompute retained statistics with an independently checked analyzer; do not modify old results.

Require exact relevant executable, library, compiled dependency, runtime-input and workload equality for same-product reuse. Any mismatch blocks reuse pending explicit applicability proof; an unexplained binary difference cannot be dismissed as immaterial. If different effective implementations remain, this same-product route is ineligible. Record the unresolved regression and obtain a separately valid matched protocol when that comparison is required; do not silently pool different implementations or promise a new A/B campaign under this amendment.

## 3. Complete obligation audit before policy activation

Categories describe evidence now, not a forecast of future PASS. A row may have completed subchecks while its aggregate obligation is incomplete.

| Mandatory obligation | Current classification | Revised action or disposition |
|---|---|---|
| C0 scope, identity, toolchain, ordinary semantics, protected work | Fully satisfied for recorded candidate; must remain true | Recheck narrow applicable closure and preserve guards |
| Affected Debug/Release compilation, ordinary instrumentation exclusion | Fully satisfied | Reuse ten builds and static/actual-flag evidence |
| Current Debug functionality | Fully satisfied | Reuse D1; no new Debug timing claim |
| Full Release finite state, support, collision/shape/inertia, render-state and work parity | Fully satisfied for executed suites; partial for final repeat set | Every added suite must pass the same assertions and match A1 |
| Complete-step, stage, fit/replacement, wake absolute CPU gates | Partially satisfied | All 1,236 executed statistics passed; M2/M3/P3 missing |
| Original standalone 2.5% repeatability | Historical FAIL, blocked solely by that measurement policy | Preserve 173 failures; already replaced by Revision03, never revive as PASS |
| Revision03 Uc<D admission and C-derived budgets | FAIL / insufficient sensitivity; blocked solely by current comparison policy | Preserve 97 failures; explicit replacement in section 4 |
| Held-out V CPU and relative overhead/non-regression | Not executed; same-product comparison requirement | Retire as a required version-delta claim; no measured regression result |
| Named-stage C-derived B and W event B | Not admitted; comparison-policy blockage | Remove B explicitly; retain all existing independent absolute limits |
| Body/contact/pass workload coverage and contrast reporting | Partially satisfied | Complete three M and three P; preserve actual work counts and all contrasts |
| Stable scaling ratios / signed-difference admission and V scaling regression | Incomplete; measurement-policy requirement | Explicit limited-claim exception; no stable-ratio/complexity-law PASS |
| Resolved S64 savings | Partially satisfied: only one M process | Complete unchanged three-M extrema rule in section 5 |
| W64 recovery and correctness | Fully satisfied in executed suites; partial for final set | Keep Amendment01 and all event/response requirements in new suites |
| Profiling perturbation admission | Partially satisfied; three M/three P not present | Compute old formula; explicit exception to its role as an absolute-budget blocker |
| C-A/A1 allocation, usable-byte coverage, ownership and teardown | Fully satisfied for current baseline | Reuse 667,701 checks / zero failures and linked coverage proof |
| Second allocation census / every-ordinal non-growth | Not executed | Fresh same-product A2; exact original <= comparison, no growth allowance |
| Zero Fit allocation, recorder nonallocation after preallocation, resource ceilings | Fully satisfied for current paths by Fit census and static recorder proof, not an armed dynamic guard | Preserve all requirements; bind static proof to exact current compiled identities |
| Phase20 <=4096-byte additional query-resource obligation | Satisfied by unchanged-provider/compiled-impact proof | Retain proof; changes invalidate this reuse |
| Ordinary Release I1 | Not executed | Mandatory automated ordinary smoke plus source/API coverage |
| Ordinary UI replacement, visual support/impact, sleep/wake and plausibility | Not executed as Phase25 human acceptance | Mandatory Owner inspection after eligible automated handoff |
| Ordinary multi-step-per-render-frame execution count | Not measured dynamically | Source/scheduling proof and honest limitation; no new dynamic count claim |
| Final review/checkpoint and Owner acceptance | Not executed / not eligible | Prepare only after all revised mandatory automated gates pass |
| Genuine production correction | None demonstrated necessary | Timing spread alone does not authorize a Physics change |

Resource usage and absence of a current absolute exceedance do not establish the missing results. Historical Phase21 stability and Phase24/Collider comparisons are residuals, not silently satisfied Phase25 obligations.

## 4. Exact retained gates, replacements and accepted risk

Retain unchanged: workload definitions/order/counts/warm-ups/windows, geometry routes, inclusive measurement boundaries, q convention/admission, independent per-process absolute limits, all correctness assertions, resolved M sleep savings, allocation/ownership/teardown/query obligations, resource limits except the named analysis increase, ordinary I1 and final human acceptance. Normal Solver equations and one-pass policy, sleep thresholds, integration, GJK/EPA and Manifold remain unchanged.

The following four dispositions must be accepted together as part of this amendment. They are not numerical PASS findings and do not receive a generic P2 label.

| Disposition | Requirement replaced | Assurance deliberately lost; retained mitigation |
|---|---|---|
| P25-CURRENT-ABSOLUTE | Revision03 Uc<D, B=L+D-Uc, all C-derived B gates, held-out V CPU inequalities and V spread tests | No 5% version-to-version regression or stationarity certification. Every process still meets original absolute budgets; all repeats, ranges, old 173/97 failures and exact identities retained. Same product has no optimization delta. |
| P25-FINITE-SCALING | Proposal01 ratio-range<=5%, ratio_B/V ratio rules and signed-difference spread/comparison gates | No stable timing ratio, asymptotic complexity or resolved timing increase for the second pass is certified. Every declared size/contact/pass workload must meet its own unchanged absolute limits, actual work must occur, and all contrasts remain reported. |
| P25-INSTRUMENTED-COST | Proposal01 abs(O)+J<=max(.10*median(M),5000ns) as a prerequisite to current absolute acceptance, including its V counterpart | No bounded or causal profiling-overhead assurance. Compute and retain the old numerical result; report FAIL if exceeded and INCONCLUSIVE if sensitivity/causality is unresolved. M and P must both pass their own absolutes and identical state/work; no overhead subtraction. Stage costs remain instrumented. P24-PERF-OBSERVER Physics residual is not closed by this exception. |
| P25-SINGLE-IMPLEMENTATION | Duplicate V sleep/scaling population and designation of A2 as a different implementation | One finite current implementation is qualified. Three-M resolved sleep remains mandatory; A2 is fresh exact allocation consistency, not a measured old/new memory regression. Historical comparisons remain unresolved. |

The Owner is accountable for accepting these narrower claims; Physics performance and performance-ledger owners retain the residuals. Risks are unmeasured old-to-new regressions, unstable cost ratios, unbounded incremental observer perturbation and unobserved execution conditions. Absolute budgets, full workloads, empirical variation disclosure, unchanged physical/ownership gates and human inspection mitigate but do not eliminate them. Revisit upon a material Physics/workload/toolchain change, an absolute-budget failure, a later joint-performance admission needing comparison, or any claim requiring stable ratios/bounded observer overhead. A later change gets its own prospective applicable performance policy; this amendment supplies no automatic future 5% regression rule. Phase26 remains independently gated, particularly by Phase21/OD06.

## 5. Fixed evaluation rules

**Per-process cost.** Retain every measured sample. Apply Proposal01 raw+q convention, including two q terms for two-step sums. Require common measured 0<q<=1000ns; existing q=100ns. Report the clock loop/common minimal observer separately without subtraction. Compute arithmetic mean, nearest-rank median/p95, total-step maximum rolling one-simulated-second mean (60 steps; L120 120), and existing W event maxima. Steps/rows within one process are correlated; the independent repeat unit is the process. Do not pool a slow process into an average of passing ones. No trimming, favorable repetition, inference of population confidence or hard-real-time maximum.

| Boundary, every applicable process | Mandatory unchanged absolute limit |
|---|---|
| Complete M/P step mean and median | <=0.50*dt |
| Complete M/P step p95 and maximum rolling-second mean | <=0.75*dt |
| Each named P stage mean/median; p95 | <=0.50*dt; <=0.75*dt respectively; stage C-derived B explicitly retired |
| Pure fitting / direct replacement, each geometry/route | mean/median<=0.5ms; p95<=1ms |
| Combined mesh-fit replacement | mean/median<=1ms; p95<=2ms |
| Following cold step | Its complete-step and stage limits independently |
| W64 command maximum | <=1ms |
| W64 first and second step maxima; pair maximum | <=dt each; <=2dt pair |

Exact dt fractions control, not rounded milliseconds. At 60Hz total reservations are 8.333333ms and 12.5ms; L120 at 120Hz uses 4.166667ms and 6.25ms. This introduces no new 2ms or 10% limit: the table's 2ms replacement limit and the diagnostic observer 10% criterion are existing Proposal01 values. Preserve p99, maximum and dt exceedances descriptively, except the already mandatory W maxima. Stages overlap and must not be added into an exclusive total. Aggregate-stage raw+q retains Revision03's empirical interpretation, not a calibrated physical upper endpoint. Exact absent work is N/A only with source and zero-invocation/work proof.

**Scaling and workload capacity.** All original 21 rows and 18,960 full-suite steps remain. Require F64 traversal; A16/A64/A256; B64/B64-P2; S64; L120; W64; all twelve direct/mesh geometry cells. Check actual source-order state/work/body/contact/pass counters and intended second Solver traversal. No arbitrary monotonicity assertion replaces physical parity. For mean/median/p95 report A64/A16, A256/A64, B64/A64, B64-P2/B64 and S64/A64 for total M/P and applicable P boundaries, Solver mandatory for pass reporting. Use a ratio only if the denominator exceeds 20q in all three processes of that mode and neither side is unresolved; otherwise report signed differences. Report all three values, ranges and source-proven N/A. Work/count coverage and each workload's absolute budgets are mandatory; diagnostic ratios have no new acceptance ceiling. No linear or universal N-body scaling claim follows.

**Resolved sleep, unchanged.** On the three M process statistics, separately for total mean/median/p95, let U_S=range(S64)+2q and U_A=range(A64)+2q. Require max(S64)+U_S < min(A64)-U_A for mean and median; use <= for p95. A numeric failed inequality remains FAIL; if uncertainty prevents resolution, the savings claim is also INCONCLUSIVE and blocks completion. No minimum savings percentage is invented. P sleep costs remain reported. W64 retains .05 rolling resistance, .1 wake, 120-step recovery and twenty Release cycles, with natural sleeping/recovery and every command/response cost gate. W64/A64 is diagnostic because the work differs.

**Profiling effects.** Using all three values per mode, calculate the existing O=median(P)-median(M), J=range(P)+range(M)+4q and threshold T=max(.10*median(M),5000ns), for all originally specified total/tail, R command and W event boundaries. Record O, J, abs(O)+J<=T and its actual outcome. Report signed observed M/P differences and ranges; negative O is not a causal speedup. An exceeded T is retained as an actual diagnostic numeric FAIL under the specifically accepted limitation in section 4; it cannot cause the absolute gates to be labeled passed if they fail. Even a numerical observer PASS is not a causal overhead proof. No new observer experiment or ceiling is introduced. Zero-loss recording, state/work parity, allocation coverage/storage and both modes' absolutes remain mandatory.

## 6. Allocation and physical correctness

Reuse fresh post-Collider C-A as A1, including all 18,960 ordinals, row setup/teardown and actual linked allocation coverage. Run one fresh A2 using the same A product and full suite. Compare every original matched ordinal and row aggregate: allocation counts; requested and queried usable bytes; temporary requested/usable peaks; survivors; retained ownership; totals/peaks; final teardown. Every required A2 value must be <= its corresponding A1 value. State/work/ordinal coverage must match exactly. No percentage-growth allowance, discarded ordinal or requested-for-usable substitution. Usable-size uncertainty is INCONCLUSIVE and blocks this gate. A2 times are not CPU evidence.

Retain zero allocations in every measured successful Fit; zero recorder allocations after preallocation; no new ordinary production retained state/allocation sites; zero fixture/Physics-owned live records at teardown; <=8MiB temporary usable per operation, <=64MiB live Physics-owned heap, <=64MiB recorder storage; existing <=4096-byte additional query-resource proof. Keep outside-scope app caches separately attributed. The dormant `Phase25Memory::observing` flag is not evidence of an armed dynamic recorder-allocation guard. The design supporting-proof.json binds the explicit current source-path proof: fixed memory tables and Record array, one Initialize preallocation, scalar Begin/End, fixed-slot writes, byte-loop hashing and nonallocating snapshot getters. Use this static proof plus existing census/coverage evidence; never claim the dormant sentinel ran. Recheck its exact compiled applicability before reuse; missing applicability is incomplete and blocks.

Retain all finite-state, support/no-floor-crossing/<=.035 settled penetration, natural sleep, exact stable sleeping pose, wake/recovery, shape/inertia, physical-state preservation, render/material/scale, identity, ownership and clean-shutdown checks. W64 Amendment01 and L120's exact historical inputs/short-run assertions remain. Do not import a 110–120s stability claim into L120's 15s observation. Current D1 and successful full Release checks are reusable; each additional full process must pass its own applicable assertions. None of this resolves Phase21's frozen stability failures.

## 7. Ordinary integration and the human boundary

I1 remains one **ordinary, uninstrumented Release** RBS process, <=120s: own and identify its SDL window, require responsiveness within the existing 60s startup limit, observe at least ten seconds of responsive normal event-loop operation, require the current-process fresh application log (Log.cpp opens it with truncation), request native close, require exit0/quiescence without forced termination, and inspect retained current-process logs for application/GL/shutdown failures. No Phase25/Phase24 qualification definitions or request environment may be active. Preserve and verify actual O product/toolchain/runtime identities, including assets/settings before I1; a missing log is incomplete, not evidence of no errors. These narrow runner checks and verification of immutable A1 raw files before A2 comparison are required routine preparation. Reuse source/API/compile proof for current support, replacement and resource paths explicitly; do not describe it as an automated UI action.

| Topic | What automated I1 and supporting proof establish | What they do not establish / required Owner coverage |
|---|---|---|
| Physics step scheduling | Ordinary event loop runs; unchanged source has the single fixed-step scheduler and wall-time path | Responsiveness does not prove how many steps actually occurred |
| Multiple steps per rendered frame | Source proof: accumulator allows zero/one/two steps per update, with 0.25s pending-time cap | No measured per-frame step-count guarantee; no special scheduler instrumentation is added |
| Collider editing/replacement | Current API functional fixtures and ordinary compilation/source connection | I1 issues no edit gesture; Owner must perform ordinary Sphere/Box/Capsule edit, fit and replacement |
| Renderer/Physics interaction | Ordinary startup/responsiveness, source connection to rendering and available error diagnostics | No observed rendered-frame progress, frame-time or GPU-delivery qualification; Owner observes bodies and rendered transforms together |
| Resources/shutdown | Current ownership/census proof plus successful ordinary normal close | No universal process-heap leak or long-duration stability guarantee |
| Physical plausibility | Deterministic fixture assertions are separate evidence | Owner must visually inspect support/impact, sleep/wake and replacement behavior |

The final human inspection must select a body, use Body Transform / Pause simulation and resume, and observe motion/support/impacts and sleep/wake. Exercise Proposed Collider Type (Sphere/Box/Capsule), Fit to Mesh and manual dimensions, Apply to Active Object, Current Collider/Applied readback, and Revert. Cancel an overlap warning unless deliberately choosing its stated replacement risk. Use existing Transform and motion controls for an intentional wake/impact where needed; inspect collider outline/mesh, material/scale and absence of stale visuals, then close normally and report actual errors. The UI uses ReplaceBodyColliderIfNoAddedOverlap and confirmed replacement paths; qualifier API calls alone do not test this UI/admission interaction. A visual inspection does not count individual Physics steps per frame. Explicit dynamic catch-up measurement or general gameplay throughput is outside this finite claim and requires a separately specified workload/observer/resource contract if later needed.

There is no universal 60FPS, frame-time, general gameplay or rendering guarantee. The sum of multiple Physics steps, rendering and other work may exceed a frame budget even when a single step meets this reservation. Broader gameplay performance remains a distinct future integration/performance obligation of the Owner and Physics/Renderer owners, before asserting such a guarantee or materially expanding workloads; existing Phase40 and Phase52/53 obligations retain their own scope, without silently assigning them new tests here.

## 8. One fixed minimum-sufficient execution plan after acceptance

No new native process or build is authorized by this design step. After explicit acceptance:

1. Record the exact accepted amendment hash, cumulative ledger and small acceptance matrix in a new evidence destination. Adapt only the existing runner/evaluator authority, labels, gates and orchestration. Preserve its previous bytes. Verify raw-statistic evaluation, all input/product reuse, source membership and ordinary compilation flags. No rebuild or D1 repetition is required for a proven runner-only change.
2. Admit **all** saved M1/P1/P2, A1 and D1 evidence by exact applicability. Historical acquisition dates/order remain untouched. The break between original and added observations is an explicitly accepted second collection session, not uninterrupted Revision03 spacing or an assumed stationary machine.
3. Run exactly **M2 -> M3 -> P3**, each full suite with <=600s deadline, same M/P products and original row order. Initial creation request is 30s after final preflight; subsequent requests target 30s after previous owned-job quiescence. Request and actual process creation must be within target..target+1s. Pre-stage products and metadata. No timing-dependent cooling, affinity/power changes, unrelated process termination or insertion of favorable samples.
4. Evaluate all six CPU processes and unchanged resolved sleep gates. Freeze the complete absolute/coverage/sleep decisions and explicitly exceptioned diagnostic outcomes. Any unexceptioned failure stops dependent qualification. This analysis barrier is declared in advance; no C-derived performance budget is selected.
5. Run **A2** full suite, <=600s, 30s after completed admission/preflight, and perform every exact allocation comparison. On PASS, complete the ordinary compatibility record. This allocation-analysis barrier precedes I1.
6. Run **ordinary I1**, <=120s, 30s after its final preflight, with the same creation lateness rule. On all revised automated PASS results, prepare final review, compact candidate/checkpoint inputs, exact evidence/reuse map and named accepted limitations. Stop at **VALIDATED PHASE25 CANDIDATE — OWNER RELEASE|x64 HUMAN ACCEPTANCE REQUIRED**.

Five planned future creations: three CPU, one allocation, one ordinary smoke. Three total processes per M/P mode preserve the original minimum independent repetition count. They characterize finite observed variation; no additional V CPU population is required. A2 may retain the existing fixture request label V-A internally, but its new evidence must state the same-product allocation-verification role. It is never a copied A1 result or a new version comparison.

At most two additional process creations may replace demonstrated invalid/incomplete infrastructure attempts, within the local and cumulative limits below. Archive every failure, prove the nonsemantic cause, correct the smallest admitted runner/fixture/build defect and rerun only its affected check. Valid slow results, mandatory numeric/core/memory failures, missed scheduled request/creation bounds and unexplained Physics behavior cannot be invalidated or retried. A missed acquisition deadline stops the continuation and does not consume a reserve as permission to try again. A necessary mechanical replacement uses a recorded new preflight/30s start; any resulting session break is disclosed. No result-dependent schedule adjustment or full statistical restart is authorized. Missing inputs or evidence that cannot be recovered honestly stop. A change that invalidates existing CPU applicability does not authorize silently replacing the six-process evidence set.

## 9. Resources: actual history and prospective bounds

The immutable Revision03 closure records builds3018.967s, native1892.704s, analysis10017.832617s, combined13546.503587s, 18 launches and 94,682,359 raw bytes. This design's actual charge and exact cumulative totals are appended in `logs/pre_editor/phase-25/game-acceptance-design01/closure.json`; the waiting interval after the prior closure is excluded as Owner review time. The design itself is capped at720s and stays under the existing10800s cumulative analysis ceiling. Parallel reviewers count once within active wall-time, not as extra elapsed sessions. No historical ledger is reset or rewritten, including the prior2834.978569/2700 overrun.

| Resource | Proposed continuation bound and rationale |
|---|---|
| Native creations | Five planned + at most two mechanical replacements: local7, cumulative<=25; stricter than standing31. Unused old slots confer no extra sampling. |
| Native wall time | Planned maximum2520s (4*600+120); local<=3720s including two600s reserves; cumulative<=5612.704s from the recorded1892.704s. Standing11620.438s remains an additional outer cap. |
| Per process | CPU/A2<=600s; I1<=120s; any affected D1 replacement<=300s within the same local reserve |
| Build time | Existing cumulative5400s,1200s/command unchanged;3018.967s consumed. Expected zero new builds when products match. |
| Analysis | Explicitly increase cumulative ceiling10800 ->12600s only after acceptance. Additional1800s covers runner/evaluator adaptation, identity/reuse proof, final analysis and handoff; no new cause investigation. Actual design cost remains included. |
| Combined automated work | Existing cumulative30000s unchanged; include design, preparation, analysis, attempts and fixed waits, exclude recorded Owner waits. No guarantee all category maxima fit simultaneously. |
| Raw storage | Existing64MiB/process and512MiB cumulative unchanged;94,682,359bytes consumed. Reserve each prospective process's64MiB before launch, charging actual bytes and retaining all failures. |
| Other new evidence | Existing2GiB ceiling continues across Revision03, this design and continuation together, with274,858,184bytes at prior closure plus all subsequent additions; no new reset or cleanup. Derived products stay in isolated bin/bin-int roles. |

The additional native ceiling is an upper bound, not a target. If restoration/recompilation, a correction or analysis cannot fit, stop with actual consumption and one consolidated result. No extra production member, sampling experiment or automatic protocol revision follows from unused budget.

## 10. Classification and integrity rules

**PASS** means every retained mandatory automated criterion is met by the exact applicable full evidence, with the listed Owner-accepted claim limitations recorded distinctly. Stage/observer/scaling scope must appear in the final headline claim. Human acceptance remains separate. A previously failed legacy gate never becomes PASS by being removed from the revised exit claim. The existing MSV checkpoint schema must use ACCEPTED_EXCEPTION with actual_outcome and the specific Owner acceptance where an exception applies; never mark a failed historical check PASS or NOT_APPLICABLE.

**FAIL** means a measured absolute exceedance, failed mandatory savings inequality, allocation non-growth/ceiling violation, physical/ownership/integrity failure, or another retained requirement is violated. One failing process/ordinal suffices; no averaging or compensation. Stop dependent qualification and preserve the exact numeric result. **INCONCLUSIVE** means required identity, usable-byte, coverage or measurement resolution is missing; a required inconclusive result blocks. A diagnostic/exceptioned scaling or observer comparison may remain INCONCLUSIVE without blocking only under the exact accepted disposition above. It cannot hide a simultaneous mandatory FAIL.

Protect all old raw results, FAIL/INVALID/INCONCLUSIVE records, manifests, reviews, acceptance/proposal text and ledgers. Never edit the two historical failure records, original timestamps or PROPOSED wording. New analysis is stored alongside its inputs with distinct policy attribution. Preserve raw/logical index, refs, staged/unstaged diffs, preexisting untracked sources and full canonical RBS raw/index bytes. No reset/clean/stash, new worktree, destructive cleanup or archive rewrite.

Execution ownership remains the accepted three production members and existing PhysicsCostChecks.h/qualification runner. No production edit is planned. Only demonstrated routine nonsemantic corrections within that ownership and the fixed contract are autonomous. Preserve before-bytes and every failed candidate, freeze corrected evaluators/inputs before the affected check, and reuse unchanged evidence by impact proof. Accepted governance, helper/publication guards, toolchain/CRT, Renderer isolation, Solver/integration/sleep policy and allocation ownership remain binding. Resolve writers against OUTPUT_PATHS.json; reject escaping/reparse ancestors and retain all artifacts within declared roots. No hidden new instrumentation or broader test application.

## 11. Final Owner gate and residuals

After all revised automated gates pass, the Owner opens `C:/dev/GEngine-pre-editor/GEngine.sln`, performs the required Visual Studio **Release|x64 rebuild**, and launches that human build's ordinary `bin/Release/RigidBodySimulation/RigidBodySimulation.exe`, working directory `runtime/RigidBodySimulation/Release`, assets `bin/Release/assets`. Complete section7's inspection and report actual build/run outcomes. The isolated qualification executable is not the ordinary human target. Changed Owner source bytes require affected validation and a matching final inspection; preserve them. An automated smoke is never recorded as human PASS.

Only subsequent explicit **APPROVE PRE_EDITOR PHASE 25** may invoke the normal guarded internal seal/finish/publication workflow. This amendment neither seals nor approves anything. Phase21 remains FROZEN_UNAPPROVED. Phase24 performance remains INCONCLUSIVE under its accepted exception. Historical Candidate05/Phase24/Collider performance and Renderer/Phase53 lineage remain unresolved as already recorded. No Phase26, Solver change, new joint prerequisite disposition, universal gameplay-performance guarantee or hidden acceptance waiver is granted.

## 12. Authoritative references and design evidence

- Active phase: `docs/pre_editor/phases/PHASE_25.md`; C0/V0 and Phase18+ MSV addendum.
- Accepted original OD07: `docs/pre_editor/active/PHASE_25_OD07_DECISION_PROPOSAL_01.md`, SHA256 `907e7e0f8dc32e0ee4aabe930240b643b1fcb0bea81bd733cf63bb1416578371`.
- Revision03 acceptance: `docs/pre_editor/active/PHASE_25_OD07_REVISION_03_ACCEPTANCE.json`, SHA256 `61bcca1a981b478275d35a868f44bf7bfc09b459ea60761cffd73bf8513f3611`.
- Final historical review: `docs/pre_editor/active/PHASE_25_REVISION_03_QUALIFICATION_REVIEW.md`, SHA256 `09b1ee092cd6f9c0837bf00fd7a7ef89c8d733aec2d194cda3d83010a30192f6`.
- Revision03 closure: `logs/pre_editor/phase-25/revision03-01/final/closure.json`, SHA256 `23c2e847b5045a3e7ad2fa4c9494fcb276b603cdc5c9b107c42418b5959bafde`.
- Exact input/product freeze: `logs/pre_editor/phase-25/revision03-01/input-freeze.json`, SHA256 `1b745b402e378fd700bdaf84395bb84c6f18d9ec2aa077d0539d3879d965e9fc`.
- This design: `logs/pre_editor/phase-25/game-acceptance-design01/entry.json`, `identity-audit.json`, final static review and `closure.json`. The closure binds this amendment's exact final hash and resource charge without modifying active policy or the closed Revision03 ledger.
