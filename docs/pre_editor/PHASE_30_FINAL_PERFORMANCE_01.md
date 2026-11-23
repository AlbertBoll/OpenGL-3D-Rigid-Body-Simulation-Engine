# Phase 30 final Release performance comparison

**Publication gate — INCONCLUSIVE.** The pre-fix and corrected Editors both averaged approximately 6.06 ms per frame on the 165 Hz display. The main thread spent substantially more CPU time per frame in the corrected process. The pre-fix executable displayed both CPU-cost levels during its single run, so this pair cannot establish how much of the difference is attributable to the Editor changes. The camera trace also failed to establish comparable motion during Physics Running. The final Owner approval and both scoped technical exceptions retain their recorded dispositions. [Owner decision](PHASE_30_FINAL_OWNER_APPROVAL_01.md) · [Comparison results](../../logs/pre_editor/phase-30/final-performance-01/comparison.json)

## Measurements

Each cell below is in milliseconds per frame. Frame statistics pool the two marked repetitions after warm-up. The CPU column is the mean main-thread CPU time. The frame includes the full application loop.

| Scenario | Pre-fix frame mean / p95 / p99 | Final frame mean / p95 / p99 | CPU mean, pre-fix → final |
| --- | --- | --- | --- |
| Idle / Ready | 6.061 / 6.392 / 6.504 | 6.060 / 6.324 / 6.481 | 1.141 → 6.037 |
| RMB navigation | 6.061 / 6.354 / 6.530 | 6.062 / 6.346 / 6.517 | 1.490 → 6.070 |
| Object picking | 6.061 / 6.327 / 6.526 | 6.061 / 6.333 / 6.489 | 1.406 → 6.059 |
| Hierarchy / Inspector input | 6.061 / 6.333 / 6.512 | 6.060 / 6.324 / 6.517 | 1.480 → 6.021 |
| Physics Running with camera input | 6.060 / 6.377 / 6.536 | 6.061 / 6.334 / 6.504 | 1.416 → 6.027 |

The two repetitions show no consistent worsening of frame-time tails. The Windows per-thread timer reported CPU time in 15.625 ms increments, which makes the individual-frame CPU percentiles coarse. The aggregated CPU means and the independent thread-cycle counters corroborate the difference. Both captures retain every frame from startup through the end of observation. [Per-repetition metrics, intervals and raw capture identities](../../logs/pre_editor/phase-30/final-performance-01/measurements.json)

**Where the CPU time went.** At process ages 3–6 seconds, the shared swap-interval query averaged 5.107 ms in the pre-fix process and 5.028 ms in the final process. During measured idle, the same query averaged 0.002 ms pre-fix and 5.097 ms final. Mean swap-buffer call time was 5.221 ms pre-fix and 0.022 ms final. The final process used 16.75 million main-thread cycles per idle swap-interval query, a level also observed early in the pre-fix process. The identical outer probe locates the cost at this API boundary. Its internal cause and any relationship to the historical intermittent frame-pacing anomaly remain unknown. Baseline coordinate calibration took longer, so the first measured idle intervals began at process ages 46.40 and 13.52 seconds respectively. [Timing attribution](../../logs/pre_editor/phase-30/final-performance-01/comparison.json) · [Frame timing implementation](../../GEngine/src/Core/BaseApp.cpp)

**Camera and input evidence.** The stationary-camera Edit/Ready samples began at the same recorded position. Both ordinary RMB repetitions showed motion in both arms. Both logs recorded ten Motor Bar pick candidates. During Physics Running, both exercises received the same camera inputs and advanced Physics. The recorded camera matrix changed in 0 and 494 sampled frames pre-fix and in 0 and 0 final. A matched moving-camera Running comparison is therefore unavailable. The Inspector timing covers the delivered row, field, typing and cancel inputs. The retained pixels establish focus and the restored field value only. [Pre-fix intervals](../../logs/pre_editor/phase-30/final-performance-01/before/intervals.json) · [Final intervals](../../logs/pre_editor/phase-30/final-performance-01/final/intervals.json) · [Camera and CPU samples](../../logs/pre_editor/phase-30/final-performance-01/measurements.json)

## Candidates and execution

The two Release x64 processes used Jolt Motor Contact, one 1061 × 800 viewport in a 1900 × 1130 client, the same 2560 × 1440 display at 165 Hz, RTX 3070 Laptop GPU, AC power and Max Performance scheme. Both used the same project layout seed, assets, twelve runtime DLLs, swap interval 1 and configured manual cap 60. VSync governed pacing. The pre-fix timing executable was reused. The final timing executable was relinked from the approved Release objects using the same two existing frame-probe objects. An initial link attempt could not find the SDK resource compiler. Restoring the pinned SDK tool path completed the link before either Editor started. [Executable, linker, DLL and asset bindings](../../logs/pre_editor/phase-30/final-performance-01/preflight.json)

| Release x64 identity | Pre-fix | Final |
| --- | --- | --- |
| Ordinary executable SHA-256 | `f6079072536104fb841197c999b2aa46624d902346627014f0fb8b3fad4c60bb` | `78b4f0abc73fa90c3360105bbe9de5a8bda9d08c7c14f6d2a13f8c531b7ab5df` |
| Timing executable SHA-256 | `29f612538193e234bcf68c08fdade40d87ea0c48c8fd90de78a2268fbf55ec88` | `8de57f4c7a29e60a1240c64b992dcf73c45230bc53e20ecdd3951782c0e46aa2` |
| Process / exit | 8128 / 0 | 17612 / 0 |
| Wall seconds / total frames | 98.78 / 15,574 | 66.22 / 10,205 |
| Completed fixed steps | 424 | 429 |

Each arm used one authorized start and zero retries. Summed per-frame completed steps agreed with the positive changes in the world counter across resets. Clean exit and the fail-fast Step path support the same attempted counts. The two reservations bring the Phase 30 ledger to 15 starts and 13,320 administratively booked attempts. The guarded publisher remains before seal. [Authority](../../logs/pre_editor/phase-30/final-performance-01/authority.json) · [Pre-fix exit](../../logs/pre_editor/phase-30/final-performance-01/before/outcome.json) · [Final exit](../../logs/pre_editor/phase-30/final-performance-01/final/outcome.json)
