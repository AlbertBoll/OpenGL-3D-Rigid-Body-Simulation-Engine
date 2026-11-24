# Phase 31 ? Edit and Simulation

**Ordinary Release x64 built successfully. Phase 31 remains NOT READY.** The Owner accepted the golden selection Highlight. The exact binary inspected for that observation was unreported. The current Rider Wood retains its earlier visual acceptance.

## Editor behavior

| Area | Current contract |
| --- | --- |
| Edit | T/R/S and numeric XYZ share the existing preview and commit path. Release or complete a valid edit to commit once. Escape cancels. Ctrl+Z and Ctrl+Y operate on authored history. Structural collider, joint and scene-packet changes keep their explicit validation and Apply. |
| Mode transition | Play enters Simulation from either toolbar size. Pending authoring prevents entry. Pausing keeps the Simulation Inspector active. Returning after steps or a successful zero-tick runtime action uses the committed scene and generation-checked rebind. A clean zero-tick return uses the existing world. |
| Simulation | Selecting an entity shows its live body type, position and velocities where a body exists. The active body is read in constant selection size. Apply validates every target when invoked. A linear magnitude of zero with a nonzero angular vector produces an angular-only action. Positive linear magnitude and an angular vector produce a combined action. World and Local apply to both vectors. |
| Action safety | One queued record is consumed before the Scene update. The original checks cover scene generation, selection revision, live body binding, speed limits, whole-batch admission and velocity readback. An accepted action leaves the Running state intact. The existing emergency pause for a nonfinite post-write result remains in effect. |
| Panel ownership | Properties owns selected-entity authoring in Edit and live interaction in Simulation. World Settings owns the scene picker and packet actions, backend, gravity, fixed-step and supported solver/sleep settings, environment and presentation interpolation. The Content Browser remains a preset browser. Mover readbacks, impulse application points, velocity-change mode, saved experiments and provider details are available on demand. |
| Presentation | Dock layout and input capture are reused. Routine selection, camera and snap help moved to tooltips or was removed. Pending transactions, errors and the Native Experimental limitation stay visible. |

## Build and evidence

The Release build compiled the 11 RBS translation units that depend on the changed header and linked the ordinary executable. The build used 204.499 guarded seconds. Peak compiler private memory was 2,377,662,464 bytes and minimum available memory was 2,504,810,496 bytes. The Release project disables compiler and linker PDB generation. The build evidence records the corrected executable-only binding after the earlier symbol lookup failed.

- Executable at `bin/Release/RigidBodySimulation/RigidBodySimulation.exe`. SHA-256 `33e78198dabc9978a9774c1928433e621b3d87070f4661963bd42874cf15d5c1`.
- The current matrix contains 323 retained PASS, 37 NOT_APPLICABLE and 140 PENDING rows. The source and inventory checks establish routing, command ownership and compatibility pins. This revision used zero Editor processes and zero Physics steps. Runtime interaction and frame performance remain unmeasured on these bytes.
- Jolt Movers keeps its accepted motion evidence. Native Movers remains Experimental with historical contact FAIL and incremental heap attribution INCOMPLETE.

[Build and source evidence](../../logs/pre_editor/phase-31/edit-simulation-ux-01/status.json) ? [Component matrix](../../logs/pre_editor/phase-31/edit-simulation-ux-01/component-matrix-current.json) ? [Resource ledger](../../logs/pre_editor/phase-31/edit-simulation-ux-01/resources.json)

## Changed production files

| Area | Files under RigidBodySimulation |
| --- | --- |
| State, toolbar and safe update | `include/RigidBodySimulation.h`, `src/RigidBodySimulation.cpp`, `src/RbsViewportToolbar.cpp`, `src/RbsFrame.cpp` |
| Selected-body action | `include/RbsImpulseEditor.h`, `src/RbsImpulseEditor.inl` |
| Inspector, World Settings, hierarchy and camera controls | `src/RbsUi.cpp`, `src/RbsEditorUi.cpp`, `src/RbsEditorViews.cpp` |

## Focused ordinary inspection

1. In Edit, move, rotate and scale the Movers Rider, then enter a precise XYZ value. Check preview, release/Enter, Escape and one-step Undo/Redo. Verify the last committed pose survives Play and Return to Edit without a manual Reset.
2. In Running, select the dynamic Rider. Apply a linear impulse, an angular-only impulse with zero linear magnitude, then a combined Local impulse twice. Check the velocities, accepted-action serials and continued ticking. Select the kinematic platform and check the rejection or disabled action.
3. Pause and resume. Check that selection, camera navigation and live inspection remain available while authored transforms, history, materials, colliders and global settings stay locked. In Edit, check the Properties and World Settings controls, the Native limitation and existing docking.

The final Owner session remains reserved under its existing release conditions. Thumbnail visual quality and final Owner acceptance remain pending. Historical evidence is retained at its original candidate identities.

Two older viewport hints still point to Properties. Use World Settings for the ragdoll scene packet Apply and sky visibility.
