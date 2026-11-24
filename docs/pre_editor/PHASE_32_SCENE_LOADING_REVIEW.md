# Phase 32 scene loading revision

**READY FOR OWNER RBS ACCEPTANCE.** The Release Editor shows a temporary loading screen for a Content Browser scene request. Double-clicking a scene card, choosing **Load** in World Settings and dropping a preset into the viewport use the existing request transaction. The screen shows the selected name and the current stage. Body construction reports completed bodies from the Physics world. Work without a reliable total uses a moving bar segment. The small activity indicator beside the body count can move while one body is still being prepared. [Body count](../../logs/ui/phase32-loading-verified-pointer/body-active-1.png) · [Authored body restore](../../logs/ui/phase32-loading-verified-pointer/restoring.png)

## Loading behavior and cost

| Measured Jolt operation | Observed time |
|---|---:|
| Select Book from Box Stack, three corrected Release loads | 1,385–1,528 ms |
| Body construction during those loads | 1,347–1,479 ms (96.8% of elapsed time or more) |
| Reload the already-open Book scene | 2804 ms |
| Construct 48 bodies during that reload | 1396 ms |
| Restore authored body state during that reload | 1366 ms |
| Longest interval between loading presentations across those four loads | 43.2 ms |

The largest measured cost on a fresh load was body construction. Restoring previously authored body state added a second substantial stage on reload. The original convex-shape volume and inertia computations already pulse the initialization observer. Individual function costs were outside the timing boundary. Resource publication runs on its existing owner thread. The display uses the active window, its existing fonts and the existing initialization observer. The observer presents at existing work boundaries and reuses the scene’s resource requests. [Stage measurements](../../logs/pre_editor/phase-32/scene-loading-01/timing-corrected.json) · [Final reload](../../logs/pre_editor/phase-32/scene-loading-01/same-process-corrected.json)

A second Load click during construction produced one published successor. The integrated scene returned to Ready at tick zero with 55 entities, 48 bodies, 15 joints and one Jolt world. Closing the window while Sphere Diamond was building exercised the parked-scene rollback and RBS exited with code zero. The original failure diagnostic and restoration path remain in use. The prior correction captured a stale card tooltip after a double-click. The final filter preserves native releases for the SDL ImGui backend and the input manager. The corrected capture shows ordinary hover behavior after publication. [Published Editor](../../logs/ui/phase32-loading-verified-pointer/settled-pointer.png) · [Rollback log](../../logs/ui/phase32-loading-rollback-02/stdout.log) · [Pointer correction](../../logs/pre_editor/phase-32/scene-loading-01/input-release-correction.json)

## Mandatory steady-state performance

Both groups used the ordinary Release Editor with Jolt stopped at tick zero. Each fresh process supplied 120 warm frames and 240 measured frames with complete scene GPU queries. Whole-process CPU used a separate eight-second settled window and actual completed-frame counts. Each group submitted 46 opaque items in 13 draws and one skybox draw. Values below are the medians of the three run medians for the pass timers and of the three process-window results for whole-process CPU. [Frozen comparison](../../logs/pre_editor/phase-32/scene-loading-01/timing-method.json) · [Corrected results](../../logs/pre_editor/phase-32/scene-loading-01/timing-corrected.json)

| Milliseconds per frame | Pre-change integrated scene | Corrected Release |
|---|---:|---:|
| Whole-process CPU | 5.062 | 6.784 |
| CPU in measured submission passes, including Editor | 0.645 | 0.633 |
| Editor UI CPU | 0.347 | 0.339 |
| Timed scene GPU passes | 0.627 | 0.626 |

**The corrected build passed the predeclared non-regression check for this workload.** The overall process-CPU median rose 34.0% across differently paced runs. For processes presenting in about 0.035 ms, the corrected measurements of 6.784 and 7.061 ms/frame bracketed the steady control at 6.879. At about 5.1 ms in Present, measured CPU was 1.443 before and 1.300 after. The third control changed its pacing pattern during its settled window. All three measured rendering costs overlapped their control ranges and the inactive loading path adds no frame work. The original loading candidate independently flagged whole-process CPU at 5.062 to 6.990 ms/frame. In that batch one control already spent about 5.2 ms in Present before loading began. Other controls and candidates commonly spent about 0.035 ms there. CPU spent waiting elsewhere was outside the pass timers. These observations establish a presentation difference and leave its driver-level cause open. [Original measurements](../../logs/pre_editor/phase-32/scene-loading-01/timing.json) · [Control timing before the click](../../logs/pre_editor/phase-32/scene-loading-01/pacing-analysis.json)

In the final double-click session the same process and selected Book scene ran at 165.0 and 165.1 FPS, with 6.808 and 6.695 ms of process CPU per frame before and after the overlay. Those two observations include the normal Editor state change on scene publication. [Matched final session](../../logs/pre_editor/phase-32/scene-loading-01/same-process-corrected.json)

## Build, evidence and Owner inspection

The affected Debug and Release x64 RBS targets built against the admitted engine libraries. The current Release SHA-256 is `43d4bf9982cf35ecfb1e734cdd615fa2bb9838655049b583a0f58fe94614fae5`. Six RBS production files changed in this revision. The combined Phase 32 scope is eleven RBS production files. The previous ten-file scope already included the application header, transaction, scene builder and both UI dispatch files. This revision also requires `RbsLoadingScreen.h` to present stage progress on an existing Editor window and keep conflicting input out of a live transaction. Physics production usage is zero. The prior provider measurements, Editor controls, material bindings and thumbnail retain their original evidence. The six-body passive ragdoll remains experimental on Jolt and Native. Full Chapter 21 production qualification remains open. [Release build](../../logs/pre_editor/phase-32/scene-loading-01/build-Release-05/result.json) · [Debug build](../../logs/pre_editor/phase-32/scene-loading-01/build-Debug-02/result.json) · [Source impact](../../logs/pre_editor/phase-32/scene-loading-01/impact-current.json) · [Integrated Book ledger](PHASE_32_INTEGRATED_SANDBOX.md)

Launch `bin/Release/RigidBodySimulation/RigidBodySimulation.exe`. In **Content Browser**, double-click **Book Conformance**. Watch the body count and activity indicator, then confirm the overlay closes onto the selected scene. Load the same card again to see the indeterminate authored-body restoration stage. Owner visual acceptance is pending. [Current handoff](../../logs/pre_editor/phase-32/scene-loading-01/ready.json)
