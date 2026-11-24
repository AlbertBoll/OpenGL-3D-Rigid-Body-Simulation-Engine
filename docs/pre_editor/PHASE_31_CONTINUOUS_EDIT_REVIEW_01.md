# Phase 31 live Transform Inspector and continuous Edit

**Release x64 build PASS. Runtime and Owner acceptance PENDING. Phase 31 NOT READY.**

The Owner observed a rewind after returning to Edit on the previous revision. Its exact inspected executable was unreported. Both explicit mode controls queued reconstruction after Physics advanced. Reconstruction installed the authored Movers packet in a fresh world, and the Movers gesture handler required tick zero.

## Interaction on the new candidate

| Operation | Effective behavior |
| --- | --- |
| Select in Running or Paused Simulation | The Transform component shows copyable Position XYZ in metres, Rotation XYZ in degrees and Scale XYZ. Physics bodies use the viewport's interpolated presentation and the entity's render scale. Other entities use their local Scene Transform, following the Edit Inspector convention. A retired body produces an unavailable readback. |
| Physics Interaction | Current linear and angular velocities appear below Transform. The existing linear, angular and combined impulse commands retain their validation and safe update boundary. Advanced stays collapsed by default. |
| Return to Edit | Both toolbar controls stop integration in the same world. The existing pause contract settles interpolation to the last completed pose. Body identities, momentum, selection and the signed Movers controller phase survive. |
| Edit and resume | T/R/S and numeric XYZ use a render preview followed by one validated commit. An edited body retains velocity and wakes. Translating the registered platform rebases its path center at the current signed phase. The full platform sweep is checked before commit. Start, Stop, Reverse and queued commands retain their state. |
| Reset to Authored | Explicit Reset restores the session's authored starting configuration and retires the previous world's working Transform history. |
| Keep Simulation Changes | In stopped Edit, explicitly keep both Movers poses and sizes, or one selected unconstrained body's pose and scale, in the session's reset baseline. Movers promotion validates the complete proposed start. Invalid geometry leaves the prior baseline available. Initial velocities keep their authored values. This action writes no scene file. |

Read-only rows use the existing Transform labels and angle conversion. Right-click a displayed XYZ value to copy it. Ordinary Transform editing and undo remain Edit-only. The Scene still validates entity and body generations. The approved Highlight, viewport rendering and backend solvers retain their existing source identities.

## Source and build

- The mode controls and live gesture routing changed in `RigidBodySimulation/src/RbsViewportToolbar.cpp` and `RigidBodySimulation/src/RbsFrame.cpp`.
- The shared Transform component, runtime interaction panel and explicit Reset/Keep controls changed in `RigidBodySimulation/src/RbsUi.cpp` and `RigidBodySimulation/src/RbsImpulseEditor.inl`.
- RBS session baseline handling changed in `RigidBodySimulation/include/RigidBodySimulation.h` and `RigidBodySimulation/src/RigidBodySimulation.cpp`. The existing Sandbox swept-box check is shared through `RigidBodySimulation/include/RbsLaunchConfig.h` and `RigidBodySimulation/src/RbsLaunchConfig.cpp`.
- The only engine change is inside the existing stopped editor batch in `GEngine/include/GEngine/Physics/PhysicsSystem.cpp`. The center is prepared before any body commits, and published after the batch succeeds. An edit invalidates the old support/slip observation. The fixed-step driver and provider solvers are unchanged.

The guarded build compiled 12 RBS units and one engine unit, verified the single changed member of the 150-member engine library and linked the ordinary Release x64 Editor with zero compiler or linker warnings. Runtime staging checked 258 assets and copied zero. The resource debit was **175.891 of 225.110 guarded seconds**. The original candidate and failure record are retained.

**Executable** `bin/Release/RigidBodySimulation/RigidBodySimulation.exe`<br>
**SHA-256** `04f6dbccd71ef4d5350ec54b445a4fba7c3f154ba728edecf4d470e12bcf9ba4`

Nineteen offline checks cover the source paths, preserved identities and scalar rebase arithmetic. The 505-row component matrix retains 323 PASS, 37 NOT_APPLICABLE and 145 PENDING. This revision used zero Editor starts and zero attempted or completed Physics steps. Jolt Movers qualification and the Rider Wood visual PASS retain their original scope. Native Movers remains Experimental with historical contact FAIL and incremental heap INCOMPLETE. Thumbnail Owner visual quality remains PENDING.

## Focused Owner inspection

1. In Movers, set an authored Rider pose, enter Simulation, select the Rider and apply an off-centre or angular impulse. Compare the live XYZ readbacks with its visible motion. Pause and confirm the read-only fields, copy command and absence of a Transform Gizmo. Select the platform and a scene camera to check kinematic and non-Physics readback.
2. Return to Edit and drag the Rider from the stopped pose. Resume and check continuity. Inspect a deliberate platform translation for continued movement along the shifted path.
3. Use **Reset to Authored** and check the original starting pose. Separately use **Keep Simulation Changes** at a valid stopped pose, then Reset and check the promoted start.

Current evidence is bound in [the revision status](../../logs/pre_editor/phase-31/continuous-edit-01/status.json). The actual ordinary Editor interactions and live visual comparison await inspection. The final Owner reservation is preserved.
