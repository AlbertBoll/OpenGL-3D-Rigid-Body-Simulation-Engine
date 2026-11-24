# Phase 31 post-approval correction 02 — first numeric Gizmo keystroke

**Owner manual acceptance passed for Correction 02.** The Owner inspected the corrected Gizmo and accepted the current implementation. The ordinary Release x64 executable and toolbar source match the technical handoff. The independent correction follows the published Phase 31 commit `244e2c1aa4f97a87e5e1cffdc3660a7ecfc38100`. The earlier Owner review of Correction 01 remains recorded as FAIL. [Owner acceptance and checkpoint identities](../evidence/phase-31-gizmo-correction-02.json)

## Observed failure

In the ordinary Jolt Movers Editor, a single **T X 5** reached the GEngine input router and the SDL ImGui backend. The numeric field already held focus. The physical 5 produced SDL scancode `0x22`. The subsequent SDL text event contained `图新`. ImGui's scientific-number filter rejected both characters, the field stayed empty, and the Transform preview received no value. In the same process, ordinary ASCII input staged `9` then `90`, and a mouse-selected handle staged `5`. The process exited normally after 87.697 seconds with zero attempted or completed Physics steps. [Ordinary Editor trace](../../../logs/pre_editor/phase-31/post-approval-gizmo-02/debug.log) · [Process record](../../../logs/pre_editor/phase-31/post-approval-gizmo-02/exit.json)

## Correction and verification

The active Gizmo field takes digits, decimal points and signs from ordered ImGui key-down events. It consumes each matching SDL text echo once, including echoes arriving on later frames. Mouse-handle entry recognizes the same keys. The existing text widget handles caret editing and passes its value to the existing numeric parser and Transform transaction. Properties continues to receive ordinary SDL text. The overlay layout and drawing code are preserved. [Toolbar source](../../../RigidBodySimulation/src/RbsViewportToolbar.cpp)

The native check replayed the captured key and composed text through the linked SDL backend and ImGui widget. The reviewed field stayed empty. The corrected field contained `5` on its first update and still contained exactly `5` after composition. All six requested sequences passed with both separately polled keys and a single queued burst. Checks also covered a real ImGuizmo X handle, delayed echoes, invalid-Enter refocus, keypad entry, cancellation and an independent Properties field. All 29 assertions passed. The Owner subsequently reported PASS for the corrected desktop interaction as a whole. [Native input results](PHASE_31_POST_APPROVAL_GIZMO_02/input-replay.txt)

The incremental build compiled `RbsViewportToolbar.cpp` and linked the ordinary Release x64 executable in 14.968 guarded seconds, with zero compiler or linker warnings or errors. Total guarded time including native checks was 17.296 seconds of 40. The corrected executable SHA-256 is `ae4a2157a4daeca911f966acfd7a6c67a89edded2dde18f04ca6e90cc59c54cd`. [Release executable](../../../bin/Release/RigidBodySimulation/RigidBodySimulation.exe) · [Technical handoff as recorded before manual acceptance](PHASE_31_POST_APPROVAL_GIZMO_02/handoff.json)

## Manual inspection scope at handoff

The handoff covered **T X 5 Enter**, **R Y 90 Enter**, **R Z -45 Enter**, **T Z 0.25 Enter**, **T X -0.25 Escape** and **S 2 Enter** on an editable box. It also identified live preview, one Undo/Redo entry per completed gesture, Escape restoration and numeric entry on a mouse-selected handle.

The independent annotated tag `pre-editor-phase-31-gizmo-correction-02-owner-accepted` identifies the accepted correction commit. Phase 32 remains the next expected phase.
