# Phase 31 mode selection, playback and numeric presentation

**Release x64 build PASS. Phase 31 NOT READY. Live Editor inspection and F_OWNER remain PENDING.**

The Owner reported that entering Simulation started Physics. The full toolbar and compact menu shared a callback that requested the Running state. The inspected executable hash was unreported. The new callback selects the interaction mode and leaves Ready or Paused in place.

| Operation | Behavior in the corrected source |
| --- | --- |
| Edit to Simulation | Retains the world, pose and playback state. The Inspector immediately shows the read-only Transform and runtime impulse controls. |
| Start, Pause, Resume | Simulation provides one contextual playback button. The existing S, P and R shortcuts issue the corresponding explicit commands. Pause preserves the current world. Resume excludes elapsed wall time from the paused interval. |
| Stop | Enters Ready in the same world and stays in Simulation. The compact layout places Stop in the viewport menu. |
| Simulation to Edit | Stops Running at the current completed pose. Ready and Paused keep their state. Pending-edit, world and input ownership guards still apply. |
| Reset | Explicitly restores the authored starting state, returns playback to Ready and preserves the selected interaction mode on a same-scene reset. |

Ordinary floating-point controls and readbacks display two decimals, including editable and locked Transform XYZ, body velocities, material and light controls. For example, a coordinate entered as 1.234567 m displays as 1.23 m. Ctrl-click exposes the editable value at its stored precision, and Copy XYZ on a locked Transform preserves float round-trip precision. A value that would display as negative zero reads 0.00.

World Settings shows fixed steps in milliseconds. Solver tolerances, shadow bias, small sleep thresholds, applied mover frequency, inverse inertia and residual diagnostics retain additional precision where needed. Integer counts and identifiers retain their existing formats.

## Verification

The seven RBS translation units selected by the Release dependency log compiled and the ordinary executable linked with zero compiler or linker warnings. Runtime staging checked 258 files and copied zero. The build used **101.392 of 134.219 guarded seconds**, leaving **32.827 seconds**.

Executable `bin/Release/RigidBodySimulation/RigidBodySimulation.exe`

SHA-256 `9e8b0c7b9edf58dec140f2cd3aac0db426a8d5d055c093bd09a3559e8dd3ff87`

Source checks trace both toolbar layouts, the stopped scheduler, current-world preservation, Inspector permissions and the unchanged impulse delivery path. Independent Windows CRT formatting checks cover signed zero, rounding boundaries and maximum finite values. This revision consumed zero Editor starts and zero attempted or completed Physics steps. The 509-row matrix retains 323 prior PASS, 37 NOT_APPLICABLE and 149 PENDING. The final Owner inspection remains reserved.

Source changes are confined to the RBS numeric input, shared widgets and icon headers, the toolbar and scene-request implementation, the Properties and World Settings views, and the two included impulse and Jolt settings panels. The [build inputs](../../logs/pre_editor/phase-31/editor-playback-precision-01/focused-Release/source-inputs.json) list exact paths and identities. The [status record](../../logs/pre_editor/phase-31/editor-playback-precision-01/status.json) binds the build, source checks, component matrix, resource ledger and prior review.

## Focused inspection

1. In Edit, enter 1.234567 m in a writable Rider position field. Confirm that the field displays 1.23 m and reopens with the stored precision. Switch to Simulation and check that the completed-step counter stays fixed and Transform is read-only.
2. Use Start, Pause and Resume. Verify that the Rider moves only during playback, that its live readback follows the viewport and that Pause keeps Transform locked. Use Stop and inspect the retained pose.
3. Return to Edit, continue editing from that pose, then use explicit Reset and verify the authored starting pose. Check the same mode switch in the compact toolbar.
