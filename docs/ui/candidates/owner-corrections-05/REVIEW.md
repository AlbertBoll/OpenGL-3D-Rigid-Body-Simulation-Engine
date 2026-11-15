# Owner Corrections 05 — consolidated review

**AWAITING OWNER RELEASE|x64 REBUILD SOLUTION AND VISUAL ACCEPTANCE.**
This is the single campaign including the loading-screen amendment.
Phase 26 remains **HUMAN ACCEPTANCE BLOCKED**. Jolt SphereLattice remains
**HUMAN PHYSICS VISUAL FAIL**; no solver, damping, friction, restitution, sleep or
contact-cache tuning is included.

## Root causes and changes

| Requirement / finding | Implemented behavior |
| --- | --- |
| Real Camera/Light components lacked contextual authoring | Deferred typed Apply/Revert edits actual RenderCameraComponent and RenderLightComponent values, checks identity/generation and expected values, and reads back the accepted component. Camera projection, FOV/orthographic height and clipping use the existing CameraSnapshot validation. Editor Camera and active Scene Camera selection remain separate. |
| Scene camera/light objects had no selectable editor representation | Cached Lucide icons share selection with Hierarchy and Properties. Selected cameras draw their actual frustum; supported light direction/range/cone guides clip to the Viewport. These are editor overlays, not game geometry or physics bodies. Alt navigation, gizmos, toolbar and scene drops retain priority. |
| Environment lacked contextual controls | World Environment exposes sky visibility, including the renderer's implicit visible default when the optional component is absent. Ground/wall Surface material uses existing shared typed material authoring with authoritative readback; physics material remains separate. |
| Hierarchy supported only single/Ctrl selection | Shift selects exactly the visible entity range from an anchor. Ctrl+Shift unions a range; category rows never enter selection. Filtering/hidden anchors, duplicate names, stale registries and the 64-object bound have explicit atomic behavior. |
| Gizmo committed only the active object | All eligible selected transform roots preview together around their arithmetic-mean world pivot. Selected descendants inherit once. Apply prepares one atomic Scene/body batch at Update; Cancel/Esc leaves authoritative state untouched. Bounded 32-batch Ctrl+Z/Y history checks scene generation, entity/body identity, expected transforms and parent context. |
| Visible gizmo axis clicks could reach the object behind it | The old ImGuizmo move/scale hit test projected a ray intersection with an axis-normal plane. A front-facing axis plane through the eye produces an undefined projected point. Hit testing now measures the same displayed screen segment; plane controls and mesh picking remain intact. Both vendor copies carry the same narrow correction. |
| SDL displayed its native surface before GL painted, then synchronous initialization blocked presentation/events | Interactive RBS creates the requested window hidden, clears/swaps a complete dark frame before showing it, then uses one optional cooperative observer on the existing initialization thread. The existing ImGui renderer/fonts present the reference-inspired identity, stages and slim blue bar. Actual body work has counts; unknown work is indeterminate. Geometry/resource boundaries pump events, and long existing convex sampling loops yield presentation without changing their math. |

The early desktop check exposed a sky UI false negative: the Scene had a sky but
no optional Visibility component. The final correction adds/replaces that component
only on an explicit deferred edit. A first Debug startup showed a 3.389-second
event-pump gap around resource loading; additional pulses between existing texture
loads address that specific gap. Failed attempts and earlier binaries remain evidence.
Actual scale testing also exposed feedback between ImGuizmo's raw single-axis result
and the uniform group matrix. Separate raw/preview matrices and a per-gesture scale
origin fix that oscillation; final desktop Apply/Undo/Redo read 1.50/1.00/1.50.

## Capability limits and editing semantics

- The current renderer admits one directional, one point and one unshadowed spot
  light. Changing to an already occupied type is rejected before writing. Directional
  range and spot shadows are visibly disabled with explanations.
- Ambient remains the current PBR shader's fixed 0.05 factor. Sky tint/exposure/
  rotation have no Scene authoring contract. No fabricated sliders or new renderer
  are introduced. Surface materials are shared; the UI states that consequence.
- Group axes are world-space. Rotation/translation convert through real parents;
  parented live bodies, singular/mirrored transforms and shear-producing operations
  remain unsupported. Any scale handle controls a uniform group factor. Native
  scales use prepared shape validation and recomputed inertia. Jolt live collider
  scaling is disabled for the whole group; no fallback or partial change occurs.
- Apply/Undo/Redo require Ready or Paused. Scene/backend replacement and Reset retire
  history; Reset restores authored scenes. Selection changes, simulation start and
  direct Transform edits discard outstanding previews. A failed batch changes no
  object. Joint identities remain valid; required wake/contact/joint invalidation
  follows the existing edit contracts, without changing simulation parameters.
- Component/surface properties have Apply/Revert; transform batch history covers
  group gizmos. Interactive changes are session edits, not fixture rewrites or a
  newly invented scene serialization system. Physics-only Simulate remains deferred.
- Loading has no worker-thread GL/ECS access, fake percentages or arbitrary delays.
  The optional observer is absent from qualification/headless checks. Closing while
  loading cancels through the existing initialization unwind. Failures retain detailed
  logging and an explicit native error dialog. An individual synchronous driver or
  resource call cannot process SDL events until it returns; timing evidence reports
  the observed maximum gap rather than claiming uninterrupted responsiveness.

## Preservation and identities

`preservation.json` records the previous hierarchy-01 manifest and unchanged relevant
source closure. `logs/ui/owner-corrections-05/before/` retains the entry source and
both entry binaries. The Owner-rebuilt entry Release binary is distinguished from
the older candidate's tested binary; no earlier PASS is attributed to those new bytes.
Intermediate builds 03 and 04 and all failed logs/screenshots remain retained.
No historical candidate, authored physics fixture, approval record or publication
was rewritten. No commit, tag, push, seal or full qualification was performed.

## Focused results

| Check | Result and evidence under `logs/ui/owner-corrections-05/` |
| --- | --- |
| Final affected Debug/Release build | **PASS**, `build-07/{Debug,Release}`. MSVC 14.44.35207, SDK 10.0.26100.0, pinned Jolt 5.6.0. These scoped builds are not an Owner Rebuild Solution. |
| Final API/model checks on both binaries | **PASS**, `checks-04/{Debug-api,Release-api}`. Visible ranges, additive selection, atomic capacity rejection, filtered/hidden anchors, duplicate names, stale registry IDs, typed camera/light validation, preview/Cancel, full batch rejection, rotation Apply/Undo/Redo, complete-state notifications, handles/joints, stale worlds, parent/child inheritance, Native scale/inertia and explicit Jolt scale rejection. A late invalid Capsule scale rejects an already prepared earlier Box change. |
| Retained editor capabilities | **PASS** on both final binaries, same results folders: Native/Jolt collider replacement and stale/force confirmation, body properties, owned real convex vertices, hierarchy identity/parent checks. |
| Four-scene backend lifecycle | **PASS**, `checks-03/{Debug,Release}`: DistanceChain/8, SphereDiamond, SphereLattice and BoxStack, Native → Jolt → Native, injected rollback, reset, old-handle rejection and authored state preservation, 60 actual ticks per case. Debug Diamond took 204.12s within its existing 360s focused deadline. |
| Lifecycle evidence reuse | The four-scene runs used build 05, retained in `intermediate-build-05/`. Subsequent production deltas are only `RbsTransformSession.h`, `RbsViewportToolbar.cpp` (raw/uniform drag calculation) and `main.cpp` (pre-window error dialog). Physics, Scene, backend, fixture, resource/loading, lifecycle and test code are byte-identical. Final API checks reran; final desktop scene/backend, Reset and playback checks cover integration. No older binary is represented as the final binary. |
| Real group interaction | **PASS**, Jolt four-body Translate preview/Cancel/Apply/Undo/Redo (`ui-02-release`); Native four-body Rotate (`ui-03-final`); final Native scale gesture/Apply/Undo/Redo (`ui-04-release-final`). Final Jolt Diamond + Sphere consecutive single/Ctrl picks retain two identities. Shift/Ctrl+Shift ranges and full-group previews share Hierarchy/Viewport selection. |
| Real component/environment editing | **PASS**: Camera FOV and invalid clipping/Revert, camera frustum and Camera/Light overlay picks; final Debug FOV 55° Apply at 200%; final Release Light intensity 1.5 Apply and duplicate Point type rejection; sky off/on; ground roughness .4 Apply, invalid 2 rejection and Revert to .4. Editor view selection remains unchanged. |
| Responsive presentation | **PASS** inspected captures at 1920×1080, 1650×1050, 100/150/200% UI scales, narrow stacked Properties, scroll-accessible Camera actions, thumbnail reflow, preserved custom 273px left dock on restart and explicit Reset Layout. The actual display reports 144 DPI: SDL minimum is 1650×1050, corresponding to 1100×700 at 100% OS DPI. Physical multi-monitor/DPI migration was not exercised. Prior 125% icon evidence is unchanged, not rerun here. |
| Startup and cancellation | **PASS**, real dark loading captures, resize during Debug initialization, normal first Editor frame, and close during startup (`ui-06-cancel`, expected explicit cancellation/exit 1). All completed Editor sessions closed with exit 0. |
| Visible early failure | **PASS**, `startup-error-02`: exact final Release executable/DLL copies in an isolated missing-asset package display the real error dialog and exit 1. Canonical resources are untouched. The first probe's ambient environment override was ignored by RBS's explicit launch config; `startup-error/result.json` preserves that invalid test setup, not a PASS. Its launched process was closed cleanly. |

The early build failures and intermediate axis/sky/scale interaction failures remain
in their original folders. Final API checks and actual corrected gestures passed.
Allocation failure injection, every Camera/Light setting combination, physical display
migration and a new rendering performance campaign were not performed. Existing
OpenGL texture-state warnings and compiler deprecation warnings remain disclosed;
this work does not claim to resolve them. The separate stiffness visual failure and
previous rendering investigations retain their original dispositions.

## Loading timing and screenshot inspection

| Actual run | First native window observed | First captured complete dark surface | Initialization complete | Max recorded pump gap |
| --- | ---: | ---: | ---: | ---: |
| Final Release, Native Chain, UI 100% | 0.830s | 1.235s | 3.542s / 80 loading frames | 0.560s |
| Final Debug, Jolt Chain, UI 200%, resized while loading | 0.651s | 1.045s | 27.264s / 703 loading frames | 0.909s |
| Final Release, Jolt BoxStack, UI 150% | 0.842s | 1.322s | 4.073s / 87 loading frames | 0.668s |

These are individual startup observations, not statistical performance claims.
The first immediate capture in each sequence includes the desktop compositor's
window-show/resize transition; it is preserved, not used as a clean application frame.
`startup-001.png` onward provides the complete dark surface. Timing is bounded by
capture sampling, and the initial native paint precedes the first observer frame.
Debug's remaining longest cooperative gap is disclosed; no arbitrary delay or fake
percentage was added. Fast body construction may finish between screenshot samples.

Inspected against the loading reference and the persistent Hazel-inspired layout:
the background fills the client, centered identity and stage row remain aligned,
blue bar is slim, unknown work animates, and no UI labels overlap at tested sizes.
At 200% and minimum size, Properties and Content Browser intentionally scroll.
The reference's illustrative 68% and explanatory watermark are not runtime UI.

Selected actual screenshots (all beneath `logs/ui/owner-corrections-05/`):

| Capture | Review observation |
| --- | --- |
| `ui-04-release-final/startup-002.png` | Final 1920×1080 dark loading identity, truthful geometry stage and indeterminate bar. |
| `ui-05-debug-200/loading-resized.png` | Final 1650×1050, 200% loading UI after a live resize; complete paint and aligned stage indicators. |
| `ui-04-release-final/scale-preview.png`, `scale-redo.png` | Four-root presentation preview, unchanged draft readback until Apply, then uniform 1.50 scale on the authoritative active body. |
| `ui-02-release/four-body-preview.png`, `four-body-undo.png`, `four-body-redo.png` | Jolt translation batch; all four visible outlines return with the batch, including authoritative X=3.81 readback. |
| `ui-02-release/camera-frustum.png`, `camera-overlay-picked.png` | Real moved Scene Camera, projection guides and selectable icon without changing Editor Camera. |
| `ui-05-debug-200/camera-applied-200.png`, `editor-1920-200.png` | Final Debug stacked Camera fields and scroll-accessible Apply at 200%. |
| `ui-07-release-150/final-light-applied.png`, `occupied-light-rejected.png` | Final 150% contextual Light controls, accepted intensity and explicit renderer-capacity rejection. |
| `ui-03-final/sky-off.png`, `sky-on.png` | Real sky visibility changes, with ambient unsupported editing clearly disabled. |
| `ui-04-release-final/roughness-applied.png`, `invalid-roughness-rejected.png`, `material-revert.png` | Ground Surface material Apply, typed rejection, authoritative Revert. |
| `ui-04-release-final/sphere-add.png`, `narrow-properties.png` | Final Jolt selection and narrow Properties stacking. |
| `ui-08-persistence/restored-custom-layout.png`, `reset-ready.png` | Custom docking survives restart; explicit reset restores upper-left Hierarchy/lower-left Properties. Running/Pause/Reset also exercised, without accepting physics appearance. |
| `startup-error-02/asset-error-dialog.png` | Actual native early-failure dialog from the final executable. |

Final Debug SHA-256: `bd0397cd27f5460d02afe23e3acb41ec3c836e29e5a5136b78f34c2f66b6998e`.
Final Release SHA-256: `ef160b7ce958caec9ef67f9b083af2ef0709349c39161394e0a06429b9821b18`.
Exact source/product identities, retained test identities and evidence are in
`candidate-inputs.json`, `validation-results.json` and `evidence-index.json`.
Final source and binaries are retained under `logs/ui/owner-corrections-05/final/`.

**STOP:** Owner Release|x64 Rebuild Solution, launch that human build, and visually
review loading, Camera/Light/Environment authoring, range selection, group previews,
atomic Apply/Cancel/Undo/Redo and responsive layout. No Phase 26 approval or seal.
