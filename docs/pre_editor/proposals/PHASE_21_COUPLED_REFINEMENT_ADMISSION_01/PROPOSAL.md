# Phase 21 STOP-08: consolidated algorithm and execution admission 01

**PROPOSED — NOT ACTIVE. Phase 21 remains NOT READY.** This package completes the resource and algorithm decision for the existing START. It neither repeats STOP-07/reference research nor requests renewed permission for the already accepted three-production/two-support-file boundary. No source edit, build, native launch, qualification, threshold activation, approval or publication occurred in preparing it.

## One decision

Accept the concrete **loaded-component bidirectional velocity refinement** below, its fixed coverage/work limits, the nine-slot matrix and one conditional correction reserve. Prospectively replace the cumulative ceilings with **29 native launches, 4,340 charged native seconds and 5,723 charged build seconds**. Preserve every previous charge. Select the explicitly defined SphereBox support fixture and its rendered inspection route; there is no existing normal preset named `SphereBox`.

Acceptance permits continuous implementation and affected validation under a distinct `PHASE_21_COUPLED_REFINEMENT_EXECUTION_AUTHORITY_01`; it does not approve Phase 21. No additional global pass, positional iteration, timestep, material, sleep or physical acceptance change is admitted. The old three unused launch slots become part of this replacement cumulative allocation, not three additional slots on top of it. Consumed authorities remain consumed.

## Algorithm admission

The preserved graph contains **one component of 180 moving bodies**, 444 contact points through tick 72 and 480 at tick 73. Historical L120 also reaches 480. These are evidence-derived admission capacities, not a guarantee that a changed trajectory stays within them. The 36 columns are not independent components. Tick 72 lacks a floor-contact row; refining that unsupported network could accelerate the bottom layer toward an unrepresented boundary.

Freeze the following candidate before production edits:

1. Preserve the ordinary first `ManifoldCollector::Solve` traversal. Build connectivity from **all retained manifolds** through every positive-inverse-mass endpoint, including sleepers. All contact points, normals, tangent/friction and angular response belong to their shared-body component. Zero-mass boundaries do not join independent moving components.
2. Admit a complete component only if it has at least two awake moving bodies, a moving/moving contact, and a **nonintegrating static boundary** (zero inverse mass, not kinematic) with finite positive accumulated normal impulse after the ordinary traversal. Reject components containing kinematic boundaries or mixed prepared/unprepared sleeping rows. Do not wake bodies. The guard means current boundary loading, not gravity-direction support, future-contact closure or convergence.
3. Preflight the whole collector: at most **180 distinct positive-mass bodies, 480 contact points and 480 nonempty manifolds**. Classify all components before extra impulses. An overflow skips **all** refinement for that collector; it never truncates a network. Capacity fallback in the required supported lattice is a coverage/admission failure, not an alternate route to qualification PASS. Ordinary unsupported components remain baseline behavior and are reported as such.
4. Execute exactly **one reverse and one forward traversal** of the existing manifold sequence, restricted to admitted complete components. Component labels follow first body occurrence; pointer equality identifies bodies, never pointer sorting. Within each manifold preserve the existing small normal block and slot-order normal/friction/spin/rolling operations. Interleaving independent components in the retained sequence does not couple them.
5. Never re-enter `PreSolve`, repeat warm starting, rebuild Jacobians, reset resistance accumulators or change cached contact identities. Reset private preparation/decision flags for every retained contact in collector `PreSolve`, including sleeping contacts; arm only valid positive-step prepared rows. Consume the opportunity after the first traversal even on rejection or overflow. Default work is **one ordinary traversal plus two named local traversals**; LP remains two ordinary traversals plus the same two local traversals total.
6. Preserve **PostSolve unchanged**: existing one-pass translation, slop 0.02, fraction 0.25 and maximum correction 0.2. No positional re-solve or stronger correction is authorized. The preferred minimal correction addresses the demonstrated Integration compression; competing position corrections remain a measured End-state risk.

For a scalar active normal row, `K = J W Jᵀ > 0`, `λnew = max(0, λold − Jv/K)` and `Δv = W Jᵀ(λnew − λold)`. A later incident impulse changes an earlier residual by `Ji W Jjᵀ Δλj`. Revisiting the full component therefore addresses the demonstrated invalidation. Reverse then forward is the smallest revisit in both manifold directions. It is **sequential projected refinement**, not a direct block solution, contraction proof or analytical PASS. No new residual tolerance or outcome-selected stopping rule is introduced.

Normal/friction/spin/rolling accumulators continue to apply deltas and retract when support falls. Existing unilateral/Coulomb limits, effective masses, material products, restitution handling and angular-speed clamp remain. The source's `Solve` does not inject a new restitution/velocity bias on each revisit. Ordinary same-manifold block degeneracy retains its existing scalar fallback. Before a new refinement update, validate finite proposed accumulated values, impulses and float velocity results for both bodies; preflight the whole small-block application before committing its point impulses. An arithmetic failure stops further local work, preserves earlier finite operations and records a mandatory technical failure. It is not disguised as baseline fallback or repaired by damping/clamping beyond the existing semantics.

The alternatives were considered within the completed audit: ordering alone cannot guarantee all coupled rows; independent column blocks omit lateral coupling; a full frictional 480-contact block needs a substantially broader active-set/failure policy and cost. Coordinated position solving changes a second physical mechanism and can move penetrations or potential energy elsewhere. None is included in this first candidate. Failure of the selected candidate does not authorize switching alternatives or increasing its two sweeps.

### Finite work and storage

The 480-point capacity admits at most 480 normal and 960 tangent scalar rows, plus up to 480 spin and 960 rolling scalar channels: **2,880 channels total**, with only the existing enabled resistance channels active. These are **aggregate additional work bounds per fixed tick**, not per group:

| Work | Maximum |
| --- | ---: |
| Additional contact-point visits | 960 |
| Scalar normal updates / tangent-disk solves | 2,880 each |
| Tangent bisection steps | 92,160 |
| Spin attempts / rolling attempts | 960 each |
| Rolling bisection steps | 46,080 |
| Small normal-block attempts / active-set masks | 480 / 3,840 |

The scalar and block bounds are conservative alternatives, not a claim they all occur together. Count graph construction, attempted/rejected solves, actual inner-loop work and finite preflight separately. Graph collection uses at most 960 endpoint lookups against 180 pointers (172,800 pointer comparisons), 480 unions (171,840 parent hops) and 32,220 final compression hops. Roots use deterministic lower-index union, at most 179 parent links per find; no unbounded search or retry. Subsequent manifold dispatch uses the classified per-contact bits, not another graph search.

Use a **separate non-inlined graph-planning helper** with fixed scratch `sizeof <= 3,072 bytes`: 180 body pointers; byte parent/size/flags arrays; 480 byte manifold labels; bounded counters/header. It returns before solver work, storing only current-step eligibility bits in constraints. No graph arrays, contact snapshots or observer Event object remain in the collector's frame across a solve. No new heap allocation, global/TLS cache or retained body pointer is permitted. Extra per-constraint bookkeeping is at most eight bytes, preferably existing padding; actual added retained storage is `4 × manifold-vector capacity × added constraint bytes`, not live contact count. Measure compiler layout and actual vector capacity.

The **existing 4,096-byte compiled additional-capacity gate remains binding**. The historical proof already uses maxima for disjoint query/admission/solve paths; it records Debug query 4,088 and solver 3,304 bytes. Do not add the graph scratch around the existing solver frame or treat its source size as compiled proof. Re-establish the changed path and common-ancestor bounds from the admitted Debug/Release builds. Preserve the 64 MiB named-storage, 8 MiB observer, 2 MiB incremental pair/attribution and applicable 8/64 MiB export ceilings. Preserve the separate Phase20 4 MiB query and 1 MiB post-audit bounds. Any inability to meet these gates stops before a native launch; no memory-limit amendment is implicit.

### Exact implementation boundary

| File | Intended change |
| --- | --- |
| `GEngine/include/GEngine/Physics/Manifold.cpp` | PreSolve metadata reset; complete graph admission helper; once-per-step reverse/forward dispatch; unchanged shared small-block mathematics; work/coverage summaries. |
| `GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.h` | Private friend access, preparation/eligibility/sweep bytes, private refinement entry and bounded counter type; no external API or new resource ownership. |
| `GEngine/include/GEngine/Physics/Constraints/ConstraintPenetration.cpp` | Share existing solve mathematics through a private refinement entry; counters and pre-mutation finite guards for extra work; retain accumulated impulse/material semantics and unchanged PostSolve. |
| `RigidBodySimulation/tests/RestingContactDiagnosis.h` | Focused direct checks, bounded work summaries, affected energy/contact attribution, two support fixtures and explicit rendered support route. |
| `tools/pre_editor_phase21_qualification.py` | New authority/accounting, affected GEngine/RBS builds, matrix dispatch and original Phase20 protocol reuse/analysis, immutable evidence and reserve controls. |

No `Manifold.h`, PhysicsProfile, PhysicsSystem, scene constructor, main/default, public API, GJK/EPA, CCD, joint or dependency change. The extra traversal uses the same small-block helper and the same constraint solve routines; only their short dispatch is repeated in the collector because the existing public manifold entry has no counter argument. Private counters are passed by reference, not persisted. Existing read-only ContactSnapshot reason codes carry zero-state-change work summaries; actual impulse events retain typed identities and distinguish ordinary/reverse/forward work. Observers never drive eligibility or stopping.

## Complete validation allocation

Build the affected GEngine/RBS Debug closure first, reuse unchanged dependencies, then close Debug functional/P1 gates before Release work. One affected Release product serves all Release modes using existing runtime dispatch. The old proof that GEngine libraries are unchanged must be replaced by actual affected compilation. No new PhysicsTests native process is admitted; its full solution rebuild remains the Owner gate.

The frozen order inserts the short Phase20 compatibility process early to detect a costly regression before the long cases; it retains the original seven cases' relative order and every existing duration.

| Slot | Process and purpose | Simulated workload | Native deadline |
| --- | --- | ---: | ---: |
| C1 | Debug qualification + bounded non-simulation direct invariants | L120 prefix 15 s + stack60 20 s + wake60 10 s | 180 s |
| C2 | Release minimal-observation prefix | 15 s / 1,800 ticks | 90 s |
| C3 | Original Phase20 candidate correctness/CPU protocol | 11 episodes, 68 s / 4,980 ticks | 120 s |
| C4 | Release L120 formal qualification; compare C2 physical prefix | 120 s / 14,400 ticks | 240 s |
| C5 | Release L60 formal qualification | 120 s / 7,200 ticks | 240 s |
| C6 | Release small stack/wake | stack60 20 s + stack120 20 s + wake60 10 s | 180 s |
| C7 | Release LA sensitivity, sleep disabled | 120 s / 14,400 ticks | 240 s |
| C8 | Release LP sensitivity, two global passes | 120 s / 14,400 ticks | 240 s |
| C9 | Release SphereBox-support and SphereBoxStack support | 20 s each at 60 Hz; 2,400 ticks total | 180 s |
| **Total** | **9 processes** | **698 simulated seconds** | **1,710 seconds** |

C1 direct checks share the existing process: full-network membership and lateral links; unsupported/static/kinematic/sleeping and capacity cases; deterministic once-per-step/LP behavior; no repeated warm impulses; normal/friction/restitution/spin/rolling and denominator guards; coherent small blocks; finite abort; unchanged PostSolve and identity/lifecycle. They do not add a simulated episode. Debug's historic 15.422-second headroom is a risk, not permission to extend its deadline.

C3 preserves the exact 50 query calls, 32 helper rows, 21 physical gates, 36 original mean/p95 comparisons and five-block spread/comparability checks. Reuse immutable Control02 only when machine/topology/power, priority/affinity, toolchain/CRT/options, stimuli and measurement boundaries remain comparable. The last known machine evidence is not a guarantee of future comparability. Recheck metadata before builds; if a new control campaign is genuinely necessary, this package does not invent one. Do not rerun unaffected standalone query investigations. Existing Phase20 analysis functions may be consumed read-only; their historical runners/authorities are not modified or invoked to spend old budgets.

OD-07 timings, contact/solver counts, observer costs, allocations, mean/p95/tail and named-resource usage are collected in these required processes. There is no extra OD-07 benchmark launch. Prefix versus the observed first 1,800 L120 ticks must preserve the same candidate's physical state; historical hashes cannot be required to match a deliberately changed response. LA/LP retain diagnostic sensitivity status and cannot rescue a failed default.

Reopen affected anchor and energy attribution for the candidate. Preserve both depth metrics and their conservative maximum; classify all discrepancy classes. Include every new local impulse in the independent work ledger, friction/resistance attribution and operation ordering; neither ledger closure nor a local work argument proves overall physical correctness. Capacity, evidence, numerical, lifecycle and clean-shutdown failures block. Commit complete typed per-case metadata before entering the next case; retain raw data and explicit completeness. No dropped samples, reconstructed originals or partial PASS.

### Explicit support identities and human visibility

`SphereBox-support` means the existing `Focused(1)` fixture: static 100×1×100 box at origin; mass-1 radius-1 sphere at (0,1.5,0), identity pose and zero velocity; friction/elasticity 0.5; resistance lengths 0.05; gravity (0,-12,0); sleep 0.05/0.02 with 0.5 s dwell. Its existing Phase20 five-second episode remains unchanged in C3. C9 prospectively extends this same fixture to 20 seconds/1,200 ticks and retains its original grounded-contact numerical predicates: after tick60 upward correction <=0.005; from tick181 depth <=0.035, vertical range <=0.005, absolute vertical speed <=0.01 and energy rise from tick181 <=0.001; final60 ticks quiet and no wakes. This expressly extends the predicates' observation window, without changing any value or substituting them for the separate Phase21 wake case. It is an explicit support-fixture selection, not a new normal application preset or retroactive longer qualification.

SphereBoxStack uses the exact normal scene recipe: 16 side-2 boxes at (2.01x,1.5+2y,0), x/y in 0..3; radius-1 sphere at (3.5,5,-20), velocity (0,0,40); the five normal boundaries, normal masses/materials and **zero** ordinary spin/rolling resistance. Observe 20 seconds at 60 Hz. Preserve applicable finite/identity/contact/normal-Coulomb/energy and supported-rest depth guards, while reporting per-body trajectories, support, quiet state, churn and penetration. Do not apply the isolated sphere's height/energy test to legitimate stack impacts/toppling/free rolling or restore superseded Phase20 all-16-terminal-quiet/raw-window/repeat prerequisites. The 20-second support observation does not prove arbitrary long-duration rest.

Current Phase21 diagnostics own a scene different from the rendered `m_ActiveScene`. Therefore implement the new opt-in support mode in the admitted header using the **passed application scene**, set 60 Hz before its first tick, retain 60 Hz across the two cases and use the existing renderable-authoring path. This provides a deliberately selectable SphereBox-support view after the human build without changing main/defaults or rendering production code. The Owner also selects and inspects the normal SphereLattice, BoxStack and SphereBoxStack presets. Deliver the exact support invocation with the validated candidate; do not claim the old diagnostic window already showed its physics scene.

## Numerical policy stays fixed

Keep sphere/sphere dynamic depth **0.20**, sphere/boundary **0.10**, settled rest **0.05**, fixed-lattice sphere/sphere recovery **200 ms**, boundary recovery **100 ms** and continuous confirmation **250 ms**. Keep the original four-box/ground-specific bounds, L120 tail mean 162/180 and every-second 153/180, L60 153/180 and 144/180, energy peak 30,542.4, finite-state and all persistent drift/jitter/churn, energy-growth, contact, lifecycle and visual guards. Per-pair episode counts, cumulative exposure and temporal distribution remain required. The 200 ms rule remains an Owner gameplay-risk budget with the previously disclosed small margin.

The prospective amendment changes the **named effective solver policy and validation allocation**, not those physical thresholds. No source-dependent PASS is inherited by modified Physics. Preserve exact-case historical Debug/Prefix/L120 PASS, P21-PEN-01/P21-PEN-02/P21-L60-DEPTH-01 FAIL and every INCONCLUSIVE result. The original 15-second lattice was not settled. No proposed policy or successful future candidate rescores old evidence.

## Resource calculation and reserve

The evidence index enumerates all **11 native and 9 build results**, with raw statuses/hashes/charges. Current totals are **829.847 native seconds** and **922.986 build seconds**. Retained limits leave **3 launches, 670.153 native seconds and 1,477.014 build seconds**.

Historical planning anchors are Debug **164.578 s**, prefix **20.860 s**, L120 **125.813 s**; Debug records **93.233 s discarded wall time**, which remains charged. Their post-helper overheads are 2.140/2.157/2.266 seconds; the maximum across all 11 attempts is **2.812 s**. Full affected-build anchors are Debug 267.875 and Release 310.093 seconds, not the later tiny incremental rebuild.

The original 590 simulated seconds are a scheduler-work lower bound, not a completion estimate. Using only observed Debug/prefix/L120 overhead and optimistically zero overhead for other cases projects **721.251 s**, already 51.098 beyond the remaining 670.153. Applying L120's 5.813-second excess to the other long/small cases projects the original seven at **744.503 s**, 74.350 over. Neither extrapolation is a hard runtime lower bound or a guarantee. The complete matrix has **698 simulated seconds**, 27.847 beyond the remaining budget even before overhead under the unchanged wall-paced scheduler.

An illustrative complete projection is `164.578 + 20.860 + 71.985 + 4×125.813 + 55.813 + 45.813 = 862.301 s`, **192.148 s** beyond remaining authority. Unknown solver cost, sleep timing and instrumentation can raise this substantially. Two extra traversals can approach three times the eligible contact-solving work; graph/finite-check/observer cost is additional. No improvement or CPU PASS is promised. Mandatory original CPU gates and OD-07 characterization determine acceptability.

| Prospective allocation | Launches | Charged native reservation | Charged build reservation |
| --- | ---: | ---: | ---: |
| Historical, retained | 11 | 829.847 s | 922.986 s |
| Initial candidate | 9 | 1,710 + 9×5 = 1,755 s | 2×1,200 = 2,400 s |
| One conditional affected-revalidation reserve | at most 9 | at most 1,755 s | at most 2×1,200 = 2,400 s |
| Exact cumulative envelope | 29 | 4,339.847 s | 5,722.986 s |
| **Requested cumulative ceilings** | **29** | **4,340 s** | **5,723 s** |

Initial work alone requires cumulative cap **20** and **2,584.847 charged native seconds**; cap 18 is two launches short even without a correction reserve. The requested increase is 15 over the current cap14, 2,840 over native1500 and 3,323 over build2400. After acceptance, unspent capacity would be **18 launches, 3,510.153 native seconds and 4,800.014 build seconds**. This includes the reserve, not permission to spend it routinely.

Native process deadlines remain unchanged. Reserve five additional charged seconds per invocation for wrapper/evidence checks, distinct from the helper's existing five-second process cleanup reserve. Five seconds is a proposed accounting allowance, not guaranteed I/O performance. Reserve the full process deadline plus five seconds before starting. Enforce the allowance; preserve and charge any overrun and stop further work rather than dropping checks or excluding time. Build 1,200-second allowances already cover full charged build calls, including wrapper work. Hard deadlines are authorization ceilings, never promises that a workload will finish. No partial case becomes PASS on timeout.

The reserve permits **one demonstrated in-scope correction wave and its affected revalidation**, not a second solver design or repeat-to-pass campaign. Use only needed slots/builds. A proven support-only defect allows identity/impact-based reuse. A production-response correction invalidates all affected physical/performance cases and may require the whole nine-slot reserve. If an implementation/runner repair cannot fit that reserve, or the actual selected algorithm fails numerical/performance requirements without an in-scope defect, preserve and stop. No extra cycle, expanded capacity, alternate ordering, threshold change, additional control run or automatic third candidate is authorized.

## Freeze, final gate and rollback

Before edits, preserve the exact approved baseline `6263b3719bfd1b20d3ed8c40abf803580857a82c`, current five-file raw bytes/hashes, canonical RBS and Owner changes/index; retain an isolated selective-remediation diff. Before each affected validation, freeze candidate/input identities, this algorithm, capacities, matrix and accepted criteria. Complete the required compiled storage/options/profile checks before launches. This package's evidence index pins the relevant existing records; it is not a replacement for future candidate identities.

After all applicable automated gates pass, stop exactly at:

`VALIDATED CANDIDATE; OWNER HUMAN VALIDATION REQUIRED BEFORE SEAL`

The Owner performs full **Visual Studio 2026 Rebuild Solution, Release|x64, including PhysicsTests**, verifies complete build success, launches human-built RBS, deliberately inspects the declared SphereLattice/BoxStack/SphereBox-support/SphereBoxStack configurations, initial impacts, settling and sustained rest, sleep/wake, interactions, continued operation and clean shutdown. Preserve normal defaults unless the Owner changes them.

Confirmed abnormal sphere-pile bouncing/separation/recontact or persistent SphereBox/SphereBoxStack resting micro-jitter is **HUMAN_VISUAL_REGRESSION_FAIL** regardless of automated PASS. Preserve candidate source/diff/evidence and Owner observations; stop all acceptance/dependent progression; safely reverse only the newly introduced Physics remediation against its exact baseline, preserving support infrastructure and unrelated Owner edits. If edits conflict, stop for the exact conflict rather than use a destructive reset. Verify rollback identity without automatically launching validation. Do not redesign/retry after visual rejection. Human PASS cannot waive numerical failure; rollback does not resolve the original L60 FAIL.

**Owner action:** accept this complete prospective admission package to activate its new algorithm/resource/fixture allocation and continue autonomously within the already accepted source scope. No further routine architecture or resource confirmation is required inside it. Until that explicit acceptance, the active STOP-08 ledger and all thresholds/authorities remain unchanged. No seal, approval, publication or next phase is authorized.

Evidence and exact identities: [EVIDENCE.json](EVIDENCE.json). Machine-readable proposal: [ADMISSION.json](ADMISSION.json). Both are prospective records, not qualification evidence.
