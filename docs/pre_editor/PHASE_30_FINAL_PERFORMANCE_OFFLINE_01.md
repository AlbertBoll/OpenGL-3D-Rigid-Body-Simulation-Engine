# Phase 30 final performance follow-up

**INCONCLUSIVE. Phase 30 publication remains on hold.** The traces locate the Idle CPU difference. The synchronization trigger and receipt of the Running camera gesture remain unresolved. Owner acceptance and both exceptions stand. This review used zero Editor starts and zero Physics steps. [Original comparison](PHASE_30_FINAL_PERFORMANCE_01.md) · [Offline measurements](../../logs/pre_editor/phase-30/final-performance-offline-01/analysis.json)

## Idle CPU

Age starts at the driver's first post-launch counter, 3.497 / 3.519 s before the respective frame observers. Values are complete-frame means in ms/frame, pre-fix / corrected. Query and swap wall times use the outer counter brackets.

| Window | Frames | Main-thread CPU | Swap interval query wall time | Swap Buffers wall time |
|---|---:|---:|---:|---:|
| Age 10–14 s, paused, same stationary camera | 659 / 659 | 6.022 / 6.022 | 4.989 / 5.107 | 0.024 / 0.021 |
| Age 35–37 s, paused, selection pass active only pre-fix | 328 / 329 | 1.524 / 6.032 | 0.002 / 5.124 | 5.043 / 0.022 |
| Two scored Idle windows per arm | 1,054 / 1,056 | 1.141 / 6.037 | 0.001 / 5.096 | 5.221 / 0.022 |

At **28.917 s**, pre-fix frame **4,162** spent 4.321 ms in the query and 4.336 ms in Swap Buffers. On the next frame these became 0.003 and 4.589 ms. Swap Buffers stayed the longer call through 98.231 s. The corrected query stayed longer from frame 2 through 65.735 s. The pre-fix switch occurred with a stationary camera, zero Physics steps, an unchanged 1,061 × 800 viewport and a selection pass already active since 24.316 s. Setup placed the first scored Idle at 49.897 s pre-fix and 17.034 s corrected. [Transition frames and matched-age windows](../../logs/pre_editor/phase-30/final-performance-offline-01/analysis.json)

The **4.895 ms/frame** scored CPU increase comprises **+5.134 ms** in the query, **−0.326 ms** in Swap Buffers and **+0.087 ms** elsewhere. Independent query counters recorded 0.005 / 16.752 million cycles per frame. Mean frame time remained 6.061 / 6.060 ms. Both builds make the context current, check ownership through SDL and the engine registry, then call `SDL_GL_GetSwapInterval()`. Present calls `SDL_GL_SwapWindow()`. Swap interval stayed at 1 and manual sleep never ran. The inner probes were empty, so the context check, SDL and driver costs cannot be separated. [Frame loop](../../GEngine/src/Core/BaseApp.cpp#L550) · [Context check](../../GEngine/include/GEngine/Core/GLContextThread.h#L88) · [SDL query](../../GEngine/src/Windows/SDLWindow.cpp#L367) · [Present](../../GEngine/src/Renderer/FrameScheduler.cpp#L249)

## Camera while Physics ran

Across the **entire unpaused intervals**, the pre-fix view changed on **0 and 494 frames** over **212 and 212 steps**. The corrected counts were **0 and 0 frames** over **215 and 214 steps**. The moving pre-fix repetition showed rotation with constant reported position. Both corrected Ready gestures also rotated the view. Extending the windows through Pause yielded no additional motion. [Camera and step counts](../../logs/pre_editor/phase-30/final-performance-offline-01/analysis.json)

After Play, the driver moved from toolbar to viewport and immediately pressed RMB. The 100 ms wait came **after** the press. Play requests activation. An inactive viewport can consume the press for activation alone, and Fly requires a press routed to the View. The trace omits pre-press activation, button destination and pointer ownership. It cannot distinguish a missed gesture from a Running-specific routing defect. [Gesture sequence](../../logs/pre_editor/phase-30/final-performance-01/capture.py#L114) · [Viewport admission](../../RigidBodySimulation/src/RigidBodySimulation.cpp#L1450) · [Fly capture](../../GEngine/src/Camera/EditorCamera.cpp#L293)

## Smallest proposed verification

One corrected Release timing process, **100 seconds maximum, 240 attempted steps maximum, one Running gesture, zero retries**. Reuse the pre-fix reference and matched settings. Reproduce its startup dwell and Idle windows near 50 and 67 s, recording whether the CPU switch occurs. Near 90 s, activate the empty viewport after Play, release and allow a frame to settle. Witness activation and cursor capture, then compare the original 3.2 s gesture with the moving pre-fix repetition using actual unpaused view changes. A persistent CPU gap or missing motion keeps the gate open. The proposed process is unreleased.
