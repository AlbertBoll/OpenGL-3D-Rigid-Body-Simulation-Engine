# Phase 30 Editor CPU risk disposition proposal 01

**D30-EDITOR-FRAME-CPU-01 · PROPOSED · Owner decision pending · publication HOLD**

The original requirement is zero reproducible performance regression from the Scene Viewport corrections. The retained comparison establishes a main-thread CPU discrepancy. Its source attribution remains **UNKNOWN**. The whole-Editor CPU non-regression result remains **NOT_DEMONSTRATED**. The initial five-scenario comparison retains its **INCONCLUSIVE** verdict. [Original comparison plan](../../../logs/pre_editor/phase-30/viewport-input-01/comparison-plan.json) · [Five-scenario results](../PHASE_30_FINAL_PERFORMANCE_01.md) · [Focused status](../../../logs/pre_editor/phase-30/final-performance-focused-01/status.json)

## Measured exposure

Both binaries used Jolt Motor Contact, a stationary camera in Edit/Ready, a 1,061 × 800 viewport, a 165 Hz display, swap interval 1 and configured cap 60 in these windows. Each used about 6.022 ms of main-thread CPU per frame at ages 10–14 s.

| Process age | Main-thread CPU, pre-fix / final | Final increase | Frame time, pre-fix / final |
|---|---:|---:|---:|
| 49.90–53.10 s | 1.188 / 6.048 ms | +4.860 ms/frame | 6.061 / 6.061 ms |
| 67.26–70.47 s | 1.097 / 6.007 ms | +4.910 ms/frame | 6.060 / 6.061 ms |

At 28.917 s the pre-fix process shifted from the expensive swap-interval query to a largely off-CPU wait in Swap Buffers. The final query remained expensive through 93.467 s. Mean frame time held near 6.06 ms, with no consistent worsening of measured tails across the five scenarios. The separate Running gesture demonstrated camera motion during completed Physics steps. The older query/skip experiment found no whole-frame CPU benefit. The mixed-refresh HDMI observation remains unverified and any topology investigation is separate. [Matched-age analysis](../PHASE_30_FINAL_PERFORMANCE_OFFLINE_01.md) · [Focused verification](../PHASE_30_FINAL_FOCUSED_PERFORMANCE_01.md) · [Source investigation](../PHASE_30_FRAME_CPU_INVESTIGATION_01.md)

## Exact Release x64 candidate

| Executable | SHA-256 |
|---|---|
| Ordinary pre-fix | `f6079072536104fb841197c999b2aa46624d902346627014f0fb8b3fad4c60bb` |
| Ordinary final | `78b4f0abc73fa90c3360105bbe9de5a8bda9d08c7c14f6d2a13f8c531b7ab5df` |
| Timing pre-fix | `29f612538193e234bcf68c08fdade40d87ea0c48c8fd90de78a2268fbf55ec88` |
| Timing final | `8de57f4c7a29e60a1240c64b992dcf73c45230bc53e20ecdd3951782c0e46aa2` |

All four files match the retained build bindings. The timing executables produced the measurements. The decision binds preflight SHA-256 `ed926957ab646d0fcf63f87b5e715a1ad3b0c46fe1546ed581844bec445092d8` and focused measurements SHA-256 `630e95f0c1a476458abea540b9e3cd3585792ef0d5c592fc6f66a12bc3837ec9`. [Build, workload and probe bindings](../../../logs/pre_editor/phase-30/final-performance-01/preflight.json) · [Measurements and trace identities](../../../logs/pre_editor/phase-30/final-performance-focused-01/measurements.json)

## Decision for the Owner

**Proposed decision.** Accept the measured excess and unresolved attribution for this candidate’s Phase 30 viewport main-thread CPU criterion for publication. Treat it as a P1 performance risk. The sustained CPU demand reduces main-thread headroom. Its frequency across launches, power impact and recovery trigger are unknown. The Owner holds the residual risk. Keep the live swap-interval query and existing VSync and cap behavior. [Risk-disposition policy](../active/PHASE_18_PLUS_MINIMUM_SUFFICIENT_VALIDATION.md#L67)

Revisit on a repeatable trigger that distinguishes the builds, telemetry linking the excess to an Editor change or display transition, or recurrence with worse frame pacing or input response. A material change to the candidate, pacing path or comparison conditions requires its own assessment. [Comparison criteria](../../../logs/pre_editor/phase-30/viewport-input-01/comparison-plan.json)

**Publication boundary.** The Phase 27–32 contract permits an independently authorized `C_COST` exception. Only explicit Owner acceptance of this proposal would allow the existing publisher row to carry `result=ACCEPTED_EXCEPTION`, `actual_outcome=NOT_DEMONSTRATED`, the original check, commands and evidence, and the specific `owner_acceptance` reference. All other criteria retain their existing requirements. The recorded manual PASS and two Motors exceptions keep their original scope. The exact Phase 29-to-30 predecessor amendment for archive `ffa89a2708f34e8c0c194c1201728b565e2ba447` and the ordinary seal and publication guards would still apply. This proposal charges zero builds, processes and Physics steps. [Cost gate](../active/PHASE_27_32_EDITOR_ACCEPTANCE_AMENDMENT_01/VALIDATION_CONTRACTS.md#L18) · [Publisher validation](../../../.codex/tools/rendering_checkpoint.py#L391) · [Owner approval and exceptions](../PHASE_30_FINAL_OWNER_APPROVAL_01.md) · [Predecessor record](../active/PHASE_30_ENTRY_EXCEPTION_ACTIVATION_01.json)
