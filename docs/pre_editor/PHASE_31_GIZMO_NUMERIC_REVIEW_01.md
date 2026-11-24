# Phase 31 precise numeric Gizmo input

**Release x64 build PASS. Owner interaction verification PENDING. Phase 31 NOT READY.**

Select an editable object and activate the Scene Viewport in Edit mode. Choose a tool with **T**, **R** or **S**, then enter one of the sequences below. A translation arrow, rotation ring or scale handle can also open the same numeric field with a stationary click or by typing during a drag.

| Keys in order | Result to inspect |
| --- | --- |
| T X 5 Enter | Translate 5.00 m along X from the starting pose. |
| R Y 90 Enter | Rotate 90.00 degrees about Y at the selected pivot. |
| R Z -45 Enter | Rotate -45.00 degrees about Z. |
| S X 2 Enter | Double the dimension along X on a box that supports the resize. |
| S 2 Enter | Double all three dimensions about the selected pivot. |

Values preview as you type. Enter commits one operation. Escape restores the gesture's starting pose. Ctrl+Z and Ctrl+Y undo and redo the completed operation. The numeric field also accepts an unmodified X, Y or Z to change axis during entry. Properties XYZ remains absolute. Typed deltas retain float precision and bypass grid and angle increments. Explicit Surface Snap, Auto Resolve and collider validation still apply.

## Focused Owner inspection

1. Choose World orientation and a box with clearance. Check each sequence in the table, undoing between cases. A plain mouse drag should still work, including uniform mouse scaling.
2. Type a signed fractional displacement with grid snapping enabled. Watch the object and Gizmo preview. Press Escape, then complete a second gesture and verify one Undo and one Redo.
3. Rotate the box, choose Local orientation, and check that X follows its rotated axis. With a supported group, compare Center and Individual Origins. A world-axis resize that would require shear should show a diagnostic and leave the committed pose intact.
4. Type a number in Properties and verify it edits only the field. In Simulation, verify that Transform remains read-only. Existing assembly-specific Ragdoll and angular-joint authoring restrictions apply.

## Implementation and evidence

The pinned ImGuizmo accessor reports the exact handle selected by the mouse. The viewport routes a keyboard tool and axis into the same Transform session. Both routes capture the original selection, derive a render preview, validate placement and use the established release, cancellation and history paths. Connected-ragdoll staging and the Movers-specific authoring and working-world transactions retain their existing ownership.

The change spans the ImGuizmo header and implementation, the RBS Transform session and application headers, and RbsViewportToolbar.cpp, RbsUi.cpp, RbsFrame.cpp and RbsEditorViews.cpp. The keyboard extension changed only RbsViewportToolbar.cpp. [Exact source identities](../../logs/pre_editor/phase-31/gizmo-keyboard-01/focused-Release/source-inputs.json)

The initial mouse implementation compiled 11 RBS and two engine translation units. The keyboard extension recompiled the viewport translation unit and linked the ordinary Release executable. Both builds passed with zero compiler or linker warnings. The final build used 14.938 guarded seconds, leaving 24.013 seconds. It verified 258 runtime files. [Build record](../../logs/pre_editor/phase-31/gizmo-keyboard-01/focused-Release/final-candidate.json)

The final executable is `bin/Release/RigidBodySimulation/RigidBodySimulation.exe`, SHA-256 `9d5d56a6657cdf0bd96faf8d6854d139fc4f099443e5c67a78676253ca1b1e53`. Offline source checks and eleven independent geometric examples cover the requested sequences, coordinate bases, group pivots and shear. Editor launches, attempted Physics steps and completed Physics steps were all zero. The final Owner session remains reserved. [Offline checks](../../logs/pre_editor/phase-31/gizmo-keyboard-01/offline-checks.json) | [Resource ledger](../../logs/pre_editor/phase-31/gizmo-keyboard-01/resources.json)

The 518-row component matrix contains 323 retained PASS, 37 NOT_APPLICABLE and 158 PENDING. Nine pending rows cover the new numeric Gizmo behavior. D_EDITOR, E_INTERACTION, F_OWNER and thumbnail Owner visual quality remain pending. Jolt Movers retains its earlier motion evidence. Native Movers retains its Experimental scope, historical contact FAIL and incremental heap INCOMPLETE. [Candidate status](../../logs/pre_editor/phase-31/gizmo-keyboard-01/status.json)
