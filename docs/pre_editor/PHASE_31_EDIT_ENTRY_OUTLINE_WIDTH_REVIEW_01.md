# Phase 31 Edit mode entry and gold outline width

**Ordinary Release x64 build PASS. Owner visual and UX acceptance PENDING. Phase 31 remains NOT READY.** The candidate is `bin/Release/RigidBodySimulation/RigidBodySimulation.exe`, SHA-256 `647d9f04c3edd0bb6227567252ec7e2fdf4e08755b713a1806f837f76a1c098f`. This revision launched zero Editors and attempted zero Physics steps. [Build products](../../logs/pre_editor/phase-31/edit-entry-outline-width-01/focused-Release/final-products.json)

## Returning to Edit

Fresh Edit/Ready already permits Transform authoring on a valid selection. The toolbar and compact menu previously paused simulation and changed the mode label. After a completed step, the same world retained its attempted-step count and simulated pose. Movers correctly rejected initial-pose editing in that state. The Owner reported needing Reset. The exact executable and step count during that observation are unverified. [Recorded findings](../../logs/pre_editor/phase-31/edit-entry-outline-width-01/findings.json)

The two explicit mode controls now pause and queue one restoration of the committed scene when the world has advanced. At the next safe Update, the application verifies the replacement backend, new world identity and zero attempted and completed steps before publishing Edit/Ready. A zero-attempt world enters Edit directly. An unfinished gesture, invalid input or pending structural edit gets a diagnostic. The user can finish, apply or revert that work and choose **Return to Edit**. A rejected restoration leaves the previous world paused.

Movers reuses its mesh and material bindings, including the Rider’s Wood handle. Valid Transform history, selection and camera identities rebind against the replacement scene. The normal and compact controls use the same action. Camera editing and Pilot keep their independent mode requests. The production changes are in `RbsViewportToolbar.cpp` and `RigidBodySimulation.cpp`. `RbsUi.cpp` and `RbsFrame.cpp` update the two initial-pose diagnostics. [Source checks](../../logs/pre_editor/phase-31/edit-entry-outline-width-01/source-review.json)

## Selected outline

The previous anti-aliasing adjustment shifted the entire contour inward on partially covered mask pixels. The selected branch in `FrameSubmission.cpp` now preserves the original 1.7-DIP solid gold core and 2.45-DIP contrast core. The uncovered fraction of a physical pixel shapes only the outer one-DIP feathers. The mask sampling, gold color, hover, depth/stencil and bounded composite passes retain their previous implementations. The revised feather adds shader arithmetic. Its GPU cost and actual edge appearance await runtime observation.

| Gold half-opacity radius on a 25%-covered seed | Original | Previous AA | Corrected |
| --- | ---: | ---: | ---: |
| 1 physical pixel per DIP | 2.200 DIP | 1.450 DIP | 2.137 DIP |
| 2 physical pixels per DIP | 2.200 DIP | 1.825 DIP | 2.163 DIP |

These are calculations from the shader. Fully covered seeds retain the original profile. All 23 generated selection shaders passed offline compilation. The focused compiler and linker work used 94.078 of the available 103.687 guarded seconds. Release staging verified 258 assets. [Outline calculations](../../logs/pre_editor/phase-31/edit-entry-outline-width-01/outline-profile.json) · [Shader results](../../logs/pre_editor/phase-31/edit-entry-outline-width-01/glsl/result.json) · [Resource ledger](../../logs/pre_editor/phase-31/edit-entry-outline-width-01/resources.json)

## Proposed focused verification

One separately authorized ordinary Release session, up to 600 seconds and 120 attempted steps cumulative across all worlds, with zero retries. The final Owner inspection reservation remains available.

1. In fresh Jolt Movers, select the Rider, commit a Transform and retain its pose, Wood appearance and Undo entry. Briefly run, confirm positive step counters, then choose Edit directly. Verify one generation change, a new world at zero attempts and completions, restored pose, immediate editing and working Undo/Redo. Exercise Paused → Edit through the compact menu. Check that a pending edit survives a blocked return.
2. In Motor Contact, compare the selected diagonal bar and curved sphere with the retained images at matched camera, zoom, framebuffer extent and DPI. Inspect unscaled physical pixels, including the dark fringe, an occluded edge and hover with multiple selections. Record existing timing observations where available. [Bar reference](../../logs/ui/phase30-applied-readbacks-01/native-bar-selected.png) · [Sphere reference](../../logs/ui/phase30-applied-readbacks-01/native-sphere-selected.png)

The 493-row matrix carries 397 retained PASS, 37 NOT_APPLICABLE and 59 PENDING. Four earlier simulation-control and dirty-switch rows need current evidence for the changed entry behavior. Jolt Movers qualification and the earlier Rider Wood approval stand. Native Movers remains Experimental with contact FAIL and incremental heap attribution INCOMPLETE. [Gate status](../../logs/pre_editor/phase-31/edit-entry-outline-width-01/status.json)
