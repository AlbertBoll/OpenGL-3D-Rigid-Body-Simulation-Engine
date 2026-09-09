# Phase 35 Review

## Status

PHASE 35 STATUS: AWAITING HUMAN REVIEW

The human-requested performance revision, complete final Debug/Release validation, two full Phase 34/final Phase 35 ABBA blocks, separate pre-optimization/final Phase 35 attribution ABBA blocks, and exact replay/counter comparisons are complete. Status remained FIX PENDING until this evidence was available. No production code changed after the interrupted validation was resumed. No commit, approved tag, push, approval, sleeping integration or later-phase work occurred.

## Objective

Build deterministic contact islands for future solver and sleep boundaries. Graph construction only, from the validated resting manifolds of the existing solver.

## Baseline Commit

`9d31f12c600611bda8620546801458bee2256411` - `physics: phase 34 sleeping state primitives`, on `physics/refactor`.

The preflight helper passed. The index was empty; unrelated modified, deleted and untracked paths were recorded and preserved. There was no Phase 35 review or approved tag at entry.

## Previous Approved Tag

`physics-phase-34-approved`, equal to HEAD at entry.

## Audit Findings Addressed

The contact-island prerequisite for `PHYS-PERF-005` (sleeping). This also supplies the deterministic island foundation discussed by the audit's future solver/parallelism roadmap. It does not close the missing sleep/wake integration finding or authorize parallel execution. No historical audit text is changed.

## Allowed Scope

Five production files, one existing dedicated test file, and this review: seven files total. `PhysicsProfile.h` is the fifth production file, added for the explicitly requested attribution. No build-system source changes.

## Files Changed

- GEngine/include/GEngine/Physics/ContactIsland.h
- GEngine/include/GEngine/Physics/Manifold.h
- GEngine/include/GEngine/Physics/PhysicsProfile.h
- GEngine/include/GEngine/Physics/PhysicsSystem.h
- GEngine/include/GEngine/Physics/PhysicsSystem.cpp
- PhysicsTests/src/main.cpp
- docs/physics/PHASE_35_REVIEW.md

## Implementation Summary

The existing minimum-identity DSU, positive-effective-mass dynamic connectivity, canonical full-identity pair ordering, boundary semantics, stale-generation rejection, snapshot timing and invalidation boundaries are preserved. Every tick rebuilds the complete graph from live bodies and retained resting manifolds after `PreSolve`. Solver traversal/equations, physical state, passive sleep state, CCD and contact expiry are unchanged.

The performance revision retains contiguous node/edge/cursor scratch in `PhysicsSystem`. It verifies current input ordering before skipping a sort; it never assumes ordering from a previous tick. Dense slot lookup is enabled only after proving consecutive, distinct slots in the current sorted live set, and every lookup still compares the complete `(slot, generation)`. Sparse slots and equal slots from different generations use full-identity binary lookup. Pair sorting/deduplication uses explicit lexicographic index comparisons with the identical canonical order.

The output still uses owning `std::vector<ContactIsland>` with the same three public member vectors and the same const getter. Each current member overwrites its output position; tails are retired after the rebuild. Roots are counted before sizing the outer vector once. The no-edge path writes ordered singleton identities directly, retires old edges/boundaries and avoids DSU/output-cursor traversal. Capacity is reused, never old connectivity. Empty inputs, topology changes, removals and slot reuse remain covered.

**Representation decision:** a flat range representation was considered by inspecting the current ownership/value API and measuring the existing representation. Replacing the public vectors would change their types; maintaining an additional flat representation plus a materialized public copy would add storage and lifecycle obligations. The measured recurring output cost is now small after removing nested-vector reconstruction. The owning public representation is retained; no unmeasured claim about flat storage or hardware cache misses is made. Cold nested-vector allocation remains a disclosed limitation.

The builder implementation now resides in the existing `PhysicsSystem.cpp`, with declarations and data types in the small header. This gives the engine and direct test callers one compiled implementation with consistent profiling configuration. The prior owning-value overload remains available; callers seeking recurring scratch reuse use the new four-argument overload. The system always uses the reusable overload.

The public snapshot remains invalidated during Update and on accepted changed `SetBodyPose`, removal before deletion, shutdown and world replacement. No-op/rejected pose edits and same-world assignment preserve it. Creation and direct type/body mutation are reflected at the next update. It still describes the last resting solve, not final integrated geometry or positive-TOI interactions.

## Performance Attribution

`PhysicsProfileSnapshot` now exposes `contactIslandBuildTimeNs` plus node, edge and output subtimers, vector-capacity-growth count and node/edge-sort counts. Build time includes scratch/output growth inside the builder; node time covers fresh body records, ordering and parent initialization; edge time covers full-identity lookup, canonicalization, sorting and deduplication; output time includes DSU joining and rebuilding the ordered result. These are graph timings, independent of solver timing. Counts accumulate under the existing profile-reset convention and compile out with profiling disabled.

Capacity-growth counts count vector backing-buffer growth events, not all CRT heap calls or bytes. An ignored allocation-hook probe independently measures `_HOOK_ALLOC`/`_HOOK_REALLOC` and `_HOOK_FREE` inside builder invocations. Its allocation pass is separate from timed samples. The system's temporary snapshot-vector bookkeeping and destruction outside the builder timer are included in whole-step timing; graph-only zero-allocation measurements do not claim the entire physics step allocates nothing.

**Measured, isolated graph probe:** nominal Release x64 `/Ox /Oi /fp:precise /MTd`, profiling enabled, logical CPU 2 affinity. Fixed 10,000 live sphere bodies, alternating Dynamic/Static (5,000 each), no simulation advancement. Separated has no edges; sparse has 250 disjoint dynamic pairs, each sharing one boundary through two contacts (750 pairs, 4,750 islands); chain connects all dynamics and 4,999 boundaries (9,998 pairs, one island). One cold build, eight warmup rebuilds, a separate allocation-counted rebuild, then 101 timed rebuilds per variant/workload. Original means the exact pre-revision algorithm, with only the same subtimers added. The original and revised results match exactly. Medians below are from this isolated process, not the Phase 34/35 whole-step ABBA protocol.

| Graph | Original build ms | Revised build ms | Original nodes / edges / output ms | Revised nodes / edges / output ms | Warm allocations/frees original -> revised |
| --- | ---: | ---: | --- | --- | --- |
| Separated | 1.5261 | 0.2036 | 0.2936 / 0.0007 / 1.2178 | 0.1828 / 0 / 0.0206 | 5/5 -> 0/0 |
| Sparse | 1.9807 | 0.2798 | 0.3218 / 0.3983 / 1.2355 | 0.1876 / 0.0195 / 0.0714 | 6/6 -> 0/0 |
| Chain | 7.7335 | 0.6315 | 0.6608 / 6.7131 / 0.3187 | 0.1872 / 0.2728 / 0.1672 | 6/6 -> 0/0 |

All three revised warm builds have zero capacity-growth events. **Derived:** graph-build reductions are approximately 86.7%, 85.9% and 91.8%, respectively. Separate stage medians need not sum to the total median; a reported zero substage is below timer resolution for that sample.

Cold builder allocations original -> revised are 52,418 -> 20,002 (separated), 52,919 -> 20,504 (sparse), and 78 -> 75 (chain). These include checked-iterator proxy allocations in the unchanged `/MTd` runtime. The installed MSVC 14.44 STL invalidates iterators when clearing a nonempty vector; empty `clear()` already has a fast return. The cost attribution therefore does not claim that clearing empty vectors alone caused the regression. Repeated nonempty vector reconstruction, temporary allocations, outer-vector moves and generic pair comparisons were the evidenced costs.

Raw probe: `bin-int/phase35-performance-revision/graph-probe.csv`; source and build project are adjacent. Initial diagnostic runs and pre-revision source/review are retained in the same directory. The first instrumented build had a timer-macro argument error, corrected before measurement. An intermediate Debug run was deliberately stopped after further graph-probe evidence warranted the final comparison/output refinement; its logs are retained with the `first-` prefix. Final validation below refers only to the completed final-code runs.

## Tests Added / Modified

171 checks under `--contact-islands`, also included in the full suite (17,846 at Phase 34 -> 18,017):

- Hand-authored chains, cycles, disconnected groups, singleton dynamics, shared static/kinematic boundaries, and zero/invalid effective mass.
- Eight body/manifold-order variants with reversed pair direction, duplicate edges and empty manifolds; exact ordered output equality.
- Thirty-two seeded sparse/dense graphs, each checked against an independent breadth-first connectivity oracle, with permuted input, and with reused output storage. An empty-world check retires all reused membership.
- Byte-for-byte preservation of live body state, including passive sleeping state and derived caches, by the pure builder.
- Removal, slot reuse, foreign-world generations, stale stamped manifold input and fresh reconnection.
- Normal stepping, current manifold connectivity, pose API no-op/rejection/invalidation, contact expiry/splitting, creation, type changes, removal, replacement, empty/worldless updates and repeated shutdown.
- Positive-TOI response remains finite and outside the resting graph.

The revision adds 38 checks: all 32 independent BFS oracle graphs also run with retained graph scratch; six additional checks cover 2,048-body singleton storage, sparse/contact/empty transitions, and sparse/equal-slot full-identity lookup. All original 133 checks remain.

The original Phase 35 first focused run passed 99/100 in each configuration. A direct normal-axis teleport fixture incorrectly assumed the existing manifold expiry rule would remove all crossed contacts. The fixture now uses tangential drift to exercise actual expiry; the separate validated pose-API test remains. This correction changes no production contact behavior; the source evidence is recorded below.

## Validation Commands

Current revision evidence is retained under ignored `bin-int/phase35-performance-revision/`; all earlier Phase 35 evidence remains in `bin-int/phase35-contact-islands/`.

```powershell
python bin-int/phase35-performance-revision/build.py Release
python bin-int/phase35-performance-revision/build.py candidate
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' bin-int/phase35-performance-revision/GraphProbe.vcxproj /p:Configuration=Release /p:Platform=x64 /p:BuildProjectReferences=false /m /v:quiet
& bin-int/phase35-performance-revision/out/GraphProbe.exe
python bin-int/phase35-performance-revision/build.py Debug
python bin-int/phase35-performance-revision/measure.py
python bin-int/phase35-performance-revision/summarize.py
python bin-int/phase35-performance-revision/measure-attribution.py
python bin-int/phase35-performance-revision/format-final-evidence.py
```

`build.py Debug/Release` invokes current MSBuild on PhysicsTests, runs `--contact-islands`, runs the full PhysicsTests executable, then builds RigidBodySimulation. `build.py candidate` builds and saves the ordinary PhysicsBenchmark executable, the attribution harness, the exact-world replay and matching Release library. Benchmark inputs/configuration are described below; no generated build-system source is changed. The application Release target was additionally rebuilt after timing work with `/p:BuildProjectReferences=false` to validate the preserved current unrelated application edits (`final-current-application-Release.log`, exit 0).

**Source/binary provenance:** `verify-resumed-source.py` reconstructed all six phase source/test files from HEAD, the preserved entry patch and the recorded edit scripts in an ignored isolated directory. All current source text matched, and final SHA-256 checks confirm no changes after resumption. The current Release library initially matched the saved candidate byte for byte. Shared root Release artifacts were later regenerated; comparison of all 119 archive members found unchanged code/data sections and relocations, with changed COFF timestamps and `.debug*`/`.chks64` metadata. Every preserved measured executable remained byte-identical. Evidence: `resumed-source-verification.json`, `final-source-hashes.json`, `measurement-binary-hashes.json`, `library-section-equivalence.json` and `final-binary-verification.log`. The saved binaries, not regenerated shared outputs, were used throughout ABBA.

The interrupted final Debug process was found still running and allowed to finish; its original sequential wrapper then ran the two ABBA blocks. No duplicate Debug run was started. Only the scheduled timed workload was active during process checks; cached idle MSBuild worker processes did not constitute active builds. All subsequent attribution measurements ran after the primary block completed; application refresh ran after all timed work. Sandbox-launch setup errors required reviewed execution outside the sandbox, with no approval-review rejection.

## Debug Result

PASS: **171/171** focused contact-island checks and **18,017/18,017** full physics checks on the final revision. PhysicsTests and RigidBodySimulation Debug x64 builds pass. Full-suite elapsed time: 540.109 s; this correctness duration is not a performance sample. The resumed process completed normally with exit 0; the earlier deliberately stopped intermediate run is separate evidence.

## Release Result

PASS: **171/171** focused contact-island checks and **18,017/18,017** full physics checks on the final revision. PhysicsTests and RigidBodySimulation Release x64 builds pass; the final current-application refresh also exits 0. Full-suite elapsed time: 73.203 s; this correctness duration is not a performance sample.

Nominal optimized x64 Release uses Visual Studio v143/MSVC 14.44, Full optimization (`/Ox /Oi`), default `/fp:precise`, and profiling enabled in the engine/benchmark harnesses. The unchanged `/MTd` override remains: allocator/runtime results are **not** representative of a true Release CRT. Existing vendor/conversion/CRT warnings remain. No build configuration fix is included.

## Stability Results

**Measured:** four baseline and four final-candidate processes for each exact exported application world. Every process completes 1,500 finite ticks / 25 simulated seconds at fixed 1/60 with one solver pass. Every emitted body-state CSV and every physical frame/counter field matches the saved pre-edit baseline exactly. This is exact equality at the existing serializer precision; no tolerance was widened.

| World | Bodies / dynamics | Body CSV SHA-256 |
| --- | ---: | --- |
| boxes | 21 / 16 | `59924d46c36424e72c5d1d6232701805ab4ac965d6d26366b3423d2a08dc014c` |
| lattice | 185 / 180 | `ad4ae1d700e870d9c7413e54fd06f903645ec6f61a3905ebb23826babfcdcc08` |

Existing box gates remain linear <=0.05, angular <=0.02, penetration <=0.035, excursion <=0.05, mean Y >=4.45, finite state, peak energy <=864 * 1.005. Both replay windows pass:

| Window | Peak linear | Peak angular | Max depth | Excursion | Min average Y | Manifolds / contacts |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| 5-18 s | 2.72497e-06 | 1.215863e-06 | 0.0190289 | 1.809258e-06 | 4.464004 | [16, 16] / [64, 64] |
| 15-20 s | 2.012405e-06 | 7.069046e-07 | 0.0190289 | 4.058772e-07 | 4.464004 | [16, 16] / [64, 64] |

The full Debug and Release suites also preserve the exact exported stack at 120 Hz: early-window peak linear 2.03366e-6, angular 6.88324e-7, depth 0.0171272, excursion 1.17294e-6, final mean Y 4.46967, 16 manifolds / 64 contacts. At both rates the full-suite peak including initial energy is 864; all state is finite.

The lattice remains a preservation result: its existing motion is unchanged, and this phase does not claim settling or sleeping. Final replay frame (equal before/after):

Linear max 2.51013374329, angular max 2.50781726837, contact depth 0.0200152397156, energy 3410.67767929, 191 manifolds / 361 contacts. Maximum energy after the first completed tick is 30236.4001105.

## Benchmark Results

**Measured:** approved Phase 34 versus final Phase 35, two sequential ABBA blocks (A/B/B/A repeated twice), four fresh processes per revision/workload, logical CPU 2 affinity, normal priority. Only one timed workload runs at a time; no overlapping builds or physics tests. All samples are retained. Each whole-step value is the median of four process medians; parenthesized ranges are the minimum-maximum process medians. Solver columns are medians of the per-process sample means for benchmark fixtures and medians of per-tick samples for replays. All percentage/difference columns are **Derived**. Affinity does not fix CPU clock, thermal state or OS scheduling.

Collision fixture: 50/100/200/500/1,000/2,000 bodies, two warmup worlds and five measured worlds per process, one 1/120 tick per world, one solver pass. This fixture measures initial graph/output allocation as well as construction.

| Bodies | Phase 34 external ms (range) | Final Phase 35 external ms (range) | Change vs Phase 34 | Solver ms Phase 34 / final Phase 35 |
| ---: | ---: | ---: | ---: | ---: |
| 50 | 2.088600 (2.082600-2.289500) | 2.207900 (2.196400-2.211900) | +5.71% | 0.610250 / 0.608510 |
| 100 | 4.252700 (4.230600-13.019300) | 4.489300 (4.444000-4.566200) | +5.56% | 1.204650 / 1.216530 |
| 200 | 8.715850 (8.587100-9.567900) | 9.005300 (8.883600-9.282500) | +3.32% | 2.392130 / 2.381840 |
| 500 | 21.580400 (20.993200-24.145400) | 22.437950 (22.286400-22.624800) | +3.97% | 6.078020 / 6.113060 |
| 1000 | 43.608350 (43.071300-46.555400) | 45.670200 (45.543000-45.743300) | +4.73% | 12.044270 / 11.910100 |
| 2000 | 92.191550 (89.881500-96.379800) | 93.247350 (91.783400-94.055400) | +1.15% | 24.975370 / 24.815090 |

Separated fixture: 100/1,000/10,000 bodies, two warmup worlds/five measured worlds per process; each has four untimed cache-warmup ticks and eight measured ticks, normalized per tick. This measures recurring cost with retained graph/output capacity.

| Bodies | Phase 34 external ms (range) | Final Phase 35 external ms (range) | Change vs Phase 34 | Solver ms Phase 34 / final Phase 35 |
| ---: | ---: | ---: | ---: | ---: |
| 100 | 0.018556 (0.018500-0.018750) | 0.020731 (0.020700-0.020900) | +11.72% | 0.000048 / 0.000073 |
| 1000 | 0.191487 (0.190587-0.207538) | 0.213331 (0.209050-0.220312) | +11.41% | 0.000068 / 0.000075 |
| 10000 | 2.102669 (2.069575-2.358487) | 2.290669 (2.257475-2.317963) | +8.94% | 0.000111 / 0.000131 |

Exact exported worlds: fixed 1/60, 1,500 ticks/25 simulated seconds, one solver pass. First 299 ticks warm up; ticks 300-1,200 inclusive provide 901 measured ticks/process. Four processes per revision/world. Candidate replay logging adds graph attribution after the timed update; physical workloads and state serialization are identical.

| World | Phase 34 external ms (range) | Final Phase 35 external ms (range) | Change | Solver ms Phase 34 / final Phase 35 | Final island-build ms (range) |
| --- | ---: | ---: | ---: | ---: | ---: |
| boxes | 3.141500 (3.137000-3.298200) | 2.998400 (2.986500-3.039800) | -4.56% | 1.765500 / 1.646200 | 0.001700 (0.001700-0.001800) |
| lattice | 17.347850 (17.270100-17.905100) | 16.392950 (16.192600-16.628300) | -5.50% | 11.814550 / 10.976100 | 0.026800 (0.026100-0.028800) |

**Interpretation:** changes in whole-step or solver timing are not attributed solely to island construction. Solver code/equations/order are unchanged, yet solver timings also move. Only the dedicated graph timer measures graph cost; improved replay totals are not evidence that construction is free.

### Comparison with the previous pre-optimization Phase 35 measurements

The previous Phase 35 values below are the saved complete ABBA aggregates from `bin-int/phase35-contact-islands/summary.json`, including the exact 4.033375 ms separated 10,000-body result cited in human feedback. Final values come from the new complete ABBA run above. **Derived** differences across these two sessions are descriptive, not a controlled same-session attribution. The subsequent instrumented ABBA comparison supplies a same-session check.

| Fixture / bodies | Previous Phase 35 external ms | Final Phase 35 external ms | Change | Previous / final solver ms |
| --- | ---: | ---: | ---: | ---: |
| collision / 50 | 2.212800 | 2.207900 | -0.22% | 0.615320 / 0.608510 |
| collision / 100 | 4.548250 | 4.489300 | -1.30% | 1.235680 / 1.216530 |
| collision / 200 | 9.097250 | 9.005300 | -1.01% | 2.445740 / 2.381840 |
| collision / 500 | 22.720350 | 22.437950 | -1.24% | 6.177950 / 6.113060 |
| collision / 1000 | 45.311500 | 45.670200 | +0.79% | 12.445290 / 11.910100 |
| collision / 2000 | 94.712600 | 93.247350 | -1.55% | 25.419740 / 24.815090 |
| separated / 100 | 0.033363 | 0.020731 | -37.86% | 0.000073 / 0.000073 |
| separated / 1000 | 0.341969 | 0.213331 | -37.62% | 0.000093 / 0.000075 |
| separated / 10000 | 4.033375 | 2.290669 | -43.21% | 0.000244 / 0.000131 |
| boxes | 3.054200 | 2.998400 | -1.83% | 1.648900 / 1.646200 |
| lattice | 16.787100 | 16.392950 | -2.35% | 10.988300 / 10.976100 |

### Dedicated attribution in the approved benchmark fixtures

**Measured:** a separate two-block ABBA run compares the instrumented pre-optimization Phase 35 binary with the final Phase 35 binary, using the same attribution harness and the same warmup/sample/workload/configuration/affinity settings. Each stage value below is the median of four process sample means. These timings do not replace the unmodified PhysicsBenchmark executable whole-step ABBA values above. Old capacity/sort counters were not instrumented; their emitted zeros are not interpreted.

| Fixture / bodies | Original island-build ms | Final island-build ms | Change | Final nodes / edges / output ms | Final capacity growths per tick |
| --- | ---: | ---: | ---: | ---: | ---: |
| collision / 50 | 0.043290 | 0.013320 | -69.23% | 0.001160 / 0.000610 / 0.011420 | 61 |
| collision / 100 | 0.085450 | 0.026990 | -68.41% | 0.001990 / 0.000910 / 0.023870 | 118 |
| collision / 200 | 0.173590 | 0.045750 | -73.64% | 0.006520 / 0.001800 / 0.035670 | 229 |
| collision / 500 | 0.431130 | 0.123860 | -71.27% | 0.014620 / 0.004020 / 0.104390 | 568 |
| collision / 1000 | 0.946580 | 0.247960 | -73.80% | 0.037370 / 0.010700 / 0.196640 | 1129 |
| collision / 2000 | 1.990890 | 0.492560 | -75.26% | 0.084840 / 0.019820 / 0.386800 | 2254 |
| separated / 100 | 0.014146 | 0.001897 | -86.59% | 0.001574 / 0.000030 / 0.000204 | 0 |
| separated / 1000 | 0.139585 | 0.018891 | -86.47% | 0.016779 / 0.000020 / 0.001991 | 0 |
| separated / 10000 | 1.625309 | 0.235009 | -85.54% | 0.208469 / 0.000032 / 0.026035 | 0 |

Collision growth counts are initial/cold allocations because every measured collision sample creates a new world. Separated samples report zero recurring growth after four warmup ticks. Sort counters for these ordered benchmark fixtures are zero in the final builder; shuffled graph sorting is covered by the deterministic tests and isolated sparse/chain probe. The boxes replay performs 40 vector growths in its first tick and none in the 901-tick measurement window. The lattice performs 3,724 growths over 1,500 ticks, including 2,271 in the measurement window (maximum 59 in a measured tick), identically in all four candidate processes. Its changing topology is therefore not allocation-free. Full per-frame graph growth counts remain in the raw replay CSVs.

**Exact equality:** all 32 primary workload processes and all 16 attribution processes match every pre-existing non-timing benchmark column/final-state fingerprint or replay physical-frame/counter field. Every body CSV has the same SHA-256 as the frozen Phase 34 replay. New graph telemetry is additional data, excluded only from old/new field-set comparisons. No physical tolerance was widened. Collision dynamics remain 25/50/100/250/500/1,000; candidates/GJK/EPA/generated contacts/manifolds/constraints remain 19/38/75/188/375/750; support calls remain 209/418/825/2,068/4,125/8,250; solver passes remain one. Separated dynamics remain 50/500/5,000 with zero candidates/contacts/solver iterations. Sleeping counters remain zero.

Raw protocols, binaries, hashes, all runs and summaries are retained under `bin-int/phase35-performance-revision/`; prior Phase 35 evidence remains under `bin-int/phase35-contact-islands/`, including the initial allocation-regression results. No samples were discarded.

## Behavior Changes

The Phase 35 result is the same read-only, deterministic resting-contact island snapshot. The performance revision preserves its owning-vector API, value/copy semantics, ordered identities/pairs and lifecycle invalidation. Physical simulation, passive sleep state, solver equations/order and body representation remain unchanged. Clients must rebuild for the added system scratch and profiler snapshot fields.

## Known Limitations

**Measured residual whole-step cost:** separated 10,000 bodies now measures 2.102669 -> 2.290669 ms versus approved Phase 34 (**Derived +0.188000 ms / +8.94%**). The previous Phase 35 aggregate was 4.033375 ms; the new final aggregate is **Derived 1.742706 ms / 43.21% lower**, with the cross-session limitation stated above. Separated 100/1,000-body overhead remains +11.72%/+11.41%; collision fixture overhead remains +1.15-5.71%. Performance approval remains a human decision.

Nested owning member vectors still allocate initially and may grow when topology changes. Scratch retains its high-water capacity until system destruction; shrinking island counts retire excess nested vectors, so later regrowth can allocate again. The zero-allocation result applies to repeated fixed-graph builder calls with retained output/scratch, not to all worlds, all graph changes, or the whole physics step. The convenience value overload allocates local scratch; the system uses the reusable overload. A flat output was considered but not implemented or timed.

No hardware cache-miss or branch-miss counters were collected. Vector-capacity growth and CRT-hook allocation counts are distinct metrics. Profiling remains single-threaded, and dedicated timers have finite resolution/overhead. Graph build is real additional work; lower exact-world total or solver timing does not establish that it is free. No sleeping speedup is claimed.

The snapshot is an epoch-local description of retained resting constraints, not a persistent island identity, complete CCD graph or sleep decision. No solver partitioning, parallel execution, sleep/wake policy, CCD or contact-expiry changes are included. Exact emitted-state/counter comparisons and existing stability tolerances are retained without widening.

## Out-of-Scope Findings

- `Manifold.cpp:240-257` expires contacts using tangential drift and signed separation. A direct write to `m_Position` that crosses a partner along an old contact normal can leave one old pair with negative signed separation, so it remains a retained constraint. The original focused fixture demonstrated this in Debug and Release. `PhysicsSystem::SetBodyPose` explicitly removes contacts at the supported pose-mutation boundary. No expiry/solver policy is changed in Phase 35.
- The unrelated `RigidBodySimulation/src/RigidBodySimulation.cpp` changed relative to the older Phase 35 entry hash during this work. Its disabled scene-option edits are outside phase ownership and were preserved. Frozen boxes/lattice exports, all six phase source/test hashes, and every measured binary are unchanged. Both current application configurations build. `.gitignore` and `_Scene.cpp` still match their older entry hashes.
- Repository-wide `git diff --check` reports the pre-existing user-owned `.gitignore:33: new blank line at EOF.` This is preserved. The existing modified scene/application files, deleted historical review documents, and untracked paths are outside phase ownership.

## git diff --check

Global command exits **2**, with the unchanged pre-existing user-owned diagnostic:

```text
.gitignore:33: new blank line at EOF.
```

The Phase 35 tracked-file check exits **0**, with no whitespace diagnostics. Both new phase files also receive `git diff --no-index --check -- NUL <path>` checks; exit 1 denotes new-file content, with no whitespace diagnostic. Git emits LF/CRLF conversion notices on existing mixed-line-ending files; their unrelated contents are preserved. The global pre-existing failure is disclosed for human review, not silently repaired.

## git status --short

```text
 M .gitignore
 M GEngine/include/GEngine/Physics/Manifold.h
 M GEngine/include/GEngine/Physics/PhysicsProfile.h
 M GEngine/include/GEngine/Physics/PhysicsSystem.cpp
 M GEngine/include/GEngine/Physics/PhysicsSystem.h
 M GEngine/src/Scene/_Scene.cpp
 M PhysicsTests/src/main.cpp
 M RigidBodySimulation/src/RigidBodySimulation.cpp
 D docs/physics/review/PHASE_00_REVIEW.md
 D docs/physics/review/PHASE_01_REVIEW.md
 D docs/physics/review/PHASE_02_REVIEW.md
 D docs/physics/review/PHASE_03_REVIEW.md
 D docs/physics/review/PHASE_04_REVIEW.md
?? .agents/
?? AGENTS.md
?? BoxStackProbe.cpp
?? GEngine/include/GEngine/Physics/ContactIsland.h
?? docs/audit/
?? docs/physics/PHASE_31_REVIEW.md
?? docs/physics/PHASE_35_REVIEW.md
?? docs/rendering/
```

Unrelated status entries are preserved. The separately observed application-file changes are recorded above; no claim is made that its older entry hash still matches. HEAD remains `9d31f12`; the index is empty and no Phase 35 approved tag exists. Only the seven listed phase files belong to this review.

## Human Decision

PENDING
