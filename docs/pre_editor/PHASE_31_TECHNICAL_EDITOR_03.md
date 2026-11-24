# Phase 31 Movers Editor correction and thumbnail

**NOT READY FOR OWNER RBS ACCEPTANCE.** All eight targeted transaction and readback rows passed. Two Content Browser rows remain open because Jolt and Native displayed the previous composition after the new thumbnail was staged. The frozen inventory now has **446 PASS, 37 NOT_APPLICABLE and 2 FAIL**. The preceding matrix and its observations are preserved. [Gate status](../../logs/pre_editor/phase-31/technical-editor-03/status.json) · [Matrix verification](../../logs/pre_editor/phase-31/technical-editor-03/component-verification.json)

## Gizmo transactions and provider readbacks

The Movers handler previously left a released gesture pending for the generic transform handler. Both handled it in the same update. Movers now consumes the release once it recognizes a bound Movers body, allowing cancellation to retire the generic Apply. A drag changes the draft, Revert discards it, and Properties Apply validates and commits it. Other bodies retain their existing dispatch. [Gesture handler](../../RigidBodySimulation/src/RbsFrame.cpp) · [Observed transactions](../../logs/pre_editor/phase-31/technical-editor-03/observation-gizmo.json)

| Effective Z in metres | Before drag | After release | After Revert | After second drag and Apply |
| --- | ---: | ---: | ---: | ---: |
| Jolt platform | 0 | 0 | 0 | 0.7422 |
| Jolt Rider | 0 | 0 | 0 | 0.7519 |
| Native platform | 0.7422 | 0.7422 | 0.7422 | 1.484 |
| Native Rider | 0.7519 | 0.7519 | 0.7519 | 1.504 |

World Settings now displays `ReadMover(platform).path.frequencyHz` and `PhysicsWorld::GetGravity()`. After Apply, Jolt reported **0.03 Hz** and **(0, −9.5, 0) m/s²**. Native reported **0.025 Hz** and **(0, −9.25, 0) m/s²**. The pre-Apply frames show the previously committed provider values while edits were pending. [Readback implementation](../../RigidBodySimulation/src/RbsUi.cpp) · [Recorded before and after values](../../logs/pre_editor/phase-31/technical-editor-03/observation-provider-readback.json)

## Thumbnail

The new Movers image is a native **2048 × 1152** crop of the ordinary Release viewport. The top, front and side of the wood Rider, the raised platform and its shadow are visible. The crop excludes Editor chrome, selection outlines and Gizmos. Decoded RGB matches the captured frame pixel for pixel. The source and staged PNG share SHA-256 `34fd1e73f2a4c38c5cd1b4b7e2e0a6239976f37c0a2c2679c3fb3db582fc1c5d`. Only the Movers PNG was replaced. [New thumbnail](../../GEngine/include/GEngine/Assets/Images/Editor/Scenes/movers.png) · [Capture and staging](../../logs/pre_editor/phase-31/technical-editor-03/thumbnail-stage.json)

The shared browser preview loader now passes the display-encoded PNG values through to ImGui. On the same previous composition, the mean absolute difference between source RGB and displayed RGB fell from **55.10 to 2.58** levels on a 0–255 scale. This verifies the color correction on a visible card. [Preview loader](../../RigidBodySimulation/src/RbsEditorUi.cpp) · [Pixel comparison](../../logs/pre_editor/phase-31/technical-editor-03/card-pixels.json)

The browser loads preset thumbnails once per process. This session started with the previous PNG on disk. Both displayed cards correlated **0.9970** with that previous image and **0.5527** with the newly staged one. The new composition therefore still needs a cold display check. **Thumbnail Owner Visual Quality remains PENDING.** The Rider Wood visual PASS retains its original scope. [Actual Jolt and Native display findings](../../logs/pre_editor/phase-31/technical-editor-03/card-display.json)

## Candidate and remaining work

The incremental Release x64 build passed. The verified executable SHA-256 is `2d8a566a413ea2cea50ba233446f9ea6b4ebf717a5a2a5d77202f8b043c316d1`. PID 17328 exited normally with code 0 after **575.250 seconds**. The single process recorded **0 attempted and 0 completed Physics steps**, with zero retries. Existing Jolt qualification and the Native Experimental contact FAIL and incremental heap INCOMPLETE retain their dispositions. [Build](../../logs/pre_editor/phase-31/technical-editor-03/ordinary-Release/result.json) · [Execution ledger](../../logs/pre_editor/phase-31/technical-editor-03/resources.json)

Closing `Jolt.scene.thumbnail` and `Native.scene.thumbnail` requires **one additional technical Release process, at most 120 seconds, zero Physics steps and zero retries**. Start with the final PNG already staged, capture the actual Jolt card, switch to Native in Ready, compare both cards with the preserved PNGs, and close. The current binary and capture are sufficient. This additional role awaits authorization. The separate final Owner inspection remains reserved.
