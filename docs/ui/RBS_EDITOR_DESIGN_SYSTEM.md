# RBS editor design system

Persistent contract for the interactive RBS editor in the Jolt integration worktree.
Read this before changing editor presentation or interactions. This is an owner-requested
UI candidate, not a PRE_EDITOR phase start, seal, approval or physics qualification.

## Visual direction

Study `reference/hazelnut-editor.png`, `reference/gengine-target-layout.png` and
`reference/rbs-current.png`. Hazel supplies compact component strips, aligned property
rows, colored axis reset buttons and a thumbnail browser. The target supplies clear
scene/selection separation and restrained blue selection. Retain GEngine's own
resources, scenes, terminology and functionality; do not copy another editor's branding.

The viewport is the dominant surface. Use dark neutral panels, quieter inputs, readable
Open Sans, modest rounding and a single blue selection accent. Avoid oversized cards,
unexplained icons, decorative whitespace and one docking window per feature.

## Reusable tokens (logical pixels at 100%)

| Token | Value / role |
| --- | --- |
| Body / heading | Existing Open Sans Regular / Bold, 16 px |
| Normal widget | 26 px high (16 px font + 5 px vertical padding each side) |
| Panel padding | 10 x 10 px |
| Item spacing / inner spacing | 8 x 6 / 4 x 4 px |
| Component strip | Full width, bold, 4 px gap before next group |
| Property label column | 29% width, capped at 124 px; wrap long labels |
| Input / panel rounding | 3 / 0 px |
| Background / panel / input | #15171C / #202329 / #14171C |
| Text / secondary / disabled | #E3E6EB / #A6AEBB / #737C8A |
| Border / hover / selected | #353B46 / #354253 / #345D87 |
| X / Y / Z | #B6454B / #438653 / #3D70AD (white axis letters) |
| Thumbnail | Preferred width 160 logical px; slider 96-320; responsive 16:9 preview |

All dimensions derive from the scale token. Apply the theme from an unscaled style,
never multiply yesterday's style. Use a crisp font atlas at supported DPI sizes and
choose the matching font at frame boundaries. Interactive Windows startup is process DPI aware. Auto scale follows the main viewport and its native window DPI;
Layout offers explicit 100/125/150/200% overrides. The scale choice persists in the existing ImGui settings file. Scale changes never reset docking.
Hovered, selected, keyboard focus and disabled states must remain distinguishable.

## Docking and navigation

Five stable roles: Hierarchy, Properties (selected entity), Viewport, World Settings,
and Content Browser / Diagnostics. World owns Physics, materials and shaders. The
bottom region contains Content Browser, Console, Profiler and Physics Debug tabs.
At 1920x1080, the default is a 24% left column (Hierarchy upper third,
selected-object Properties lower two thirds), dominant 56% center Viewport and
20% right World Settings. On default-layout creation/reset, the Hierarchy grows
at larger UI scales toward a 200 logical pixel height, capped at 55% of the left
column, so its search field does not consume all visible entity rows.
The left column spans the full workspace height: Properties reaches the BOTTOM-LEFT
edge beneath Hierarchy. Split this full-height column first; bottom tabs span only
the remaining center/right area, approximately 25% at 100% scale, growing with
fonts up to 45%. Layout version 4 installs this topology for clean startup and
Layout > Reset Layout. Recognized obsolete shipped defaults migrate once, using
window role bindings, docking IDs, tree topology and original split ratios.
Unknown or customized saved layouts remain authoritative, including floating
panels. Merely finding Properties on the right is not evidence of an old default.
Persist the migration version in the existing RbsEditor settings section; never
rebuild docking every frame or overwrite the Owner's existing settings file.

The interactive main OS window targets a minimum 1100x700 logical pixels at 100%
DPI. SDL enforces the minimum, scaled for the window's monitor DPI and capped by
that display's usable client area. Refresh limits only when they change; never
force window size every frame. Other applications and qualification startup retain
their original limits. Resizing, maximizing, fullscreen and monitor moves remain
owned by the existing window system.

Dockable panels use a practical 180 logical pixel minimum width; default/reset
splits reserve that width for both sidebars and the Viewport when space permits.
The Backend selector and Apply stack in narrow World Settings panels. Properties use
stacked labels and XYZ values when two columns no longer fit. Scroll long property
and diagnostic content. Do not force a user's split widths or rebuild the DockSpace
on resize. Keep the Viewport usable within these constraints; Reset Layout installs
the corrected default only on explicit request.

Build the default layout only for a new settings file or explicit Layout > Reset
Layout (or the strictly recognized obsolete-default migration above). Migrate old
Inspector and Console docking identities once without deleting
the old records. New features belong inside these roles. Preserve menu controls,
shortcuts, selection behavior, camera navigation, diagnostics and qualification UI.
Selecting an entity updates Properties data without transferring ImGui navigation
focus there. Consecutive single/Ctrl-clicks in the active Viewport each pick once;
only explicit panel navigation may request panel focus. Retain text/widget capture,
camera/gizmo gestures, scene-drop handling and keyboard shortcut ownership.

## Property components and interactions

Use one component-header helper, one two-column property-row helper, and common
numeric / vector widgets. Labels precede values. Units belong beside labels and in
tooltips; numeric text stays editable. Long content wraps; constrained panels scroll.
At narrow widths the label stacks above values; axis groups may stack if needed.

XYZ buttons are red X, green Y, blue Z and keyboard-focusable. Each resets only its
own value. Translation and rotation reset to zero; scale resets to one. Rotation is
shown in degrees and converted at the existing radians boundary. Drag or Ctrl+click
to type. Reject non-finite input and expose validation errors; never silently clamp
physics to make a failed operation appear successful.

Transform, Rigidbody, Collider and Distance Joint use the same components. Keep
actual capability distinctions: unsupported Jolt controls are disabled with a reason;
joint descriptors are read-only until an admitted update API exists.

Rigidbody edits are drafts bound to the exact scene generation/entity/body handle.
Apply queues one typed, validated command for the next Update boundary. Revert
reads the active body and discards the draft. Successful edits refresh authoritative
readback, recompute mass/inertia, wake affected bodies and clear dependent contact
and joint state. A motion change cannot leave a joint with no Dynamic endpoint.

| Property | Native | Jolt |
| --- | --- | --- |
| Mass, motion type, friction, restitution | Editable | Editable |
| Linear / angular damping | Fixed zero; disabled | Editable [0,1] |
| Gravity factor | Fixed one; disabled | Editable [0,10] |
| CCD | Fixed existing continuous policy; disabled | LinearCast / Discrete |
| Per-body sleep enable | Fixed existing policy; disabled | Editable |
| Per-body sleep speed thresholds / dwell | Editable | Disabled; explanation points to World Sleeping |
| World point velocity / time before sleep | Unsupported; per-body semantics retained | Editable under World Settings > Physics > Sleeping |
| Spin / rolling resistance lengths | Retained readback | Retained but unsupported; explanation visible |

Keep physics edits on validated Scene/World APIs. Interactive collider Apply is also
queued for Update, where identity and confirmation context are rechecked. Stage
Native and Jolt Sphere/Box/Capsule geometry before publishing; backend failure must
leave ownership and body/joint identity intact. The existing geometric certificates,
limits, explicit risk confirmation and rollback remain mandatory. Current collider
is cyan; proposed geometry is dashed orange, including all Box edges and Capsule
arcs. Display risk and rejection reasons in text, never as an apparently successful
Apply. Preview validity is separate from permission to commit.

Interactive edits never modify frozen qualification fixtures. Reset or loading a
preset restores its defaults. Backend Apply preserves supported **applied** collider
and body authoring at Tick 0; incompatible settings reject the whole transaction and
retain the previous world. Unapplied drafts are discarded on selection or scene
changes. Never substitute Native for a requested Jolt backend.

Jolt world Sleeping uses the pinned 5.6.0 three-point test: default point velocity
0.03 m/s and time before sleep 0.5 s. Both values and their product must be finite
and positive. Apply queues a scene-generation-bound command for Update; read back
the actual settings, wake non-static bodies and restart sleep tests without replacing
body/joint identities. Revert discards the draft; Defaults stages the pinned defaults
and still requires Apply. Reset/scene load restores preset defaults. A custom Jolt
world override blocks switching to Native with rollback; Apply Defaults first.
This interactive override never changes qualification fixtures or tunes the Owner's
reported SphereLattice stiffness. Per-body Allow sleeping remains independent.

Selection rendering must retain visible-mesh coverage, occlusion, gold ring/glow,
shadows and picking. For large selected regions, an exact GPU tile occupancy map
may reject only pixels whose entire original outline neighborhood contains zero
selected coverage. Keep the original calculation for occupied neighborhoods and
small regions. Cache resources, include partial MSAA and both selection classes,
and use GPU barriers rather than CPU waits. Performance comparisons use the same
paused state, camera, Viewport pixels and settings; report delayed GPU pass timing
separately from CPU/present pacing and one-shot picking readback. Never infer a
physics problem from camera-dependent FPS or relabel a human visual failure.

Backend selection belongs only in World Settings > Physics. Apply invokes the
existing safe transaction; retain rollback, authored parameters and Tick 0 semantics.
Preset thumbnails represent existing scenes and use existing resource loading and
RequestScene. Single click selects, Load applies, double click loads. Dragging a
thumbnail uses a versioned typed ImGui payload; only a valid Viewport drop queues
RequestScene. Never mutate worlds from rendering or stepping. Invalid payloads and
failed builds retain the current world.

The thumbnail slider persists its preferred logical width (96-320) alongside UI
scale in ImGui settings. Recompute columns from available width; distribute cells
within that width, retain 16:9 images and wrap readable labels. Reduce columns as
space shrinks and scroll vertically. Load the existing eight textures once; no
per-frame recreation, asset database or new scene loading path.

## Bounded stages and acceptance

1. Persist this contract and AGENTS reference; preserve previous candidate bytes.
2. Implement theme tokens and stable docking roles.
3. Implement and apply reusable properties, including independent axis reset.
4. Add existing-preset thumbnails and preserve Native/Jolt capability presentation.
5. Build Debug and Release RBS closure; run bounded UI/lifecycle checks and capture
   actual Release 1920x1080 client pixels. Inspect references and final captures for
   proportions, typography, alignment, spacing, clipping and control behavior.

Validation is limited to affected UI and body/collider commands, scene loading, backend switch/rollback/reset,
layout persistence, 100/125/150/200% UI scales, multiple real window sizes, narrow panels,
thumbnail reflow/drop, axis interactions and clean shutdown. Existing numerical and
performance qualification is unchanged, not rerun or newly claimed. Record failures
and corrections under a new candidate. Never overwrite the preceding handoff/evidence.
Stop for Owner Release|x64 rebuild and visual review. No PhysX, destructible mesh,
physics solver tuning, Phase 26 seal, approval or publication.


## Viewport toolbar and icon contract (Owner corrections 04)

Use three groups inside the existing Viewport image: top-left Select / Translate /
Rotate / Scale, top-center Play / Pause-Resume / Reset, top-right Display Options.
The main menu has scene selection and status, without duplicate playback buttons.
If a customized Viewport is too narrow, move the center group to a second row;
if the left and right groups cannot coexist, put Options on a third row. Compute
fit from both group extents about the center. Never overlap groups or reserve
the entire image as an input-blocking rectangle.

Lucide 0.468.0 SVGs are the source of truth: unmodified 24-unit canvas, 2-unit round
stroke, 20 logical pixel glyphs inside 32 logical pixel targets, 2 px button gaps,
4 px group padding and 10 px edge margins. Group background #1A1E24 at 94% opacity,
subtle #414A56 border, hover #39434F, active #284B70 with #A9D1FF icon tint,
normal #DCE2EA and disabled #69727E. Tooltips state action and unavailability.
No Hazel logos or proprietary image assets. Retain Lucide ISC and Feather MIT
attribution alongside SVGs. The reusable icon widget owns state rendering;
GEngine's asset registry owns textures. Offline SVG-derived 20/25/30/40-pixel
atlases provide crisp 100/125/150/200% rendering. Cache all four at initialization;
no per-frame SVG parsing, allocations for icons, or texture recreation.

Use the existing compiled ImGuizmo implementation, drawn into the Viewport's draw
list, with the same validated CameraSnapshot used for rendering. Select changes
selection only. Move/Rotate operate on all selected eligible transform roots; Scale applies a
uniform group factor through the prepared batch API, including supported Jolt shapes.
Disable parented-body gizmos because the existing physics pose convention remains
world-space. Running gizmos require explicit Live Edit and a separate presentation
preview; never mutate moving bodies from the rendering callback.
Scene-only hierarchy transforms use the actual parent's inverse world transform.
Gizmos require positive scale. Under nonuniform parent scale, disable Rotate and
Scale with an explanation; local Transform fields remain available without an
unsupported shear decomposition. Mirrored/singular parent gizmos are disabled.

Transform fields and gizmos queue entity/body/generation-bound commands for Update,
then refresh authoritative Scene/body readback. Scene/reset/backend replacement
cancels stale commands and history. Selection changes cancel only an unfinished
preview; a released gesture and previously committed history remain valid. A successful Viewport-owned Reset retains activation for
the next object click while cancelling captures and stale world input. Preserve body identity, joints, contact invalidation and
rollback implemented by the existing Scene/Physics APIs. Never write a live world
from ImGui rendering.

Edit and Simulation are explicit modes in the existing Viewport toolbar. Display
mode separately from Ready/Running/Paused state. Startup uses Edit with stepping
stopped. Entering Edit suspends stepping; returning to Simulation retains completed
edits without automatically starting. Edit T/R/S select Translate/Rotate/Scale for
actual selected entities; absent selection never turns R or S into a run command.
Simulation reserves S for Start from Ready, P for Pause from Running, and R for
Resume from Paused. Choose transform tools using the toolbar in Simulation, with
explicit Live Edit ON/OFF. Reset retains the authored-scene transaction semantics.
No new docking window or simulation owner is introduced.

Text/rename/numeric capture, popups/modals, other-window keyboard ownership,
drag/drop, key repeat/cleanup and command modifiers suppress these shortcuts.
Any held mouse button/camera capture takes priority, including RMB+S flight and
Alt camera gestures. T/R/S never modify unsupported component types or create
history by switching a mode. Toolbar/popup/gizmo gestures retain Viewport activation
for the next scene click; camera gestures and scene drag/drop keep existing routing.

Display Options exposes the real grid, selected collider outlines, all-body Physics
Debug, gizmo visibility, camera views and supported debug layers. No extra docking
windows. Play/Pause/Reset use SimulationState and RequestScene. A separate physics-only
Simulate mode is deferred: the application has no distinct supported lifecycle semantics.

Hierarchy enumerates actual Scene/ECS identities and names and follows only validated
existing parent links. Filtering includes matching descendants' ancestor paths. Keep
search controls visible above a scrolling tree; use stable entity IDs for selection,
including duplicate names. Root objects remain roots; never invent grouping parents.

### Hierarchy organization (hierarchy-01)

Group root entities into collapsible **Editor-only** Cameras, Lighting, Environment,
Physics Bodies and (when needed) Entities categories. These are not ECS entities.
Camera/light/rigidbody components determine types; sky/fog and the existing grid/axis
helpers are Environment. Bodies (including the room's static collider bodies) remain
Physics Bodies. Descendants stay under their actual parent even when their type
differs. DistanceJoint endpoints never create Transform tree edges.

Sort sibling names case-insensitively with natural numeric ordering, stable entity-ID
ties and the authored `Chain root` before `Chain link 1` through `Chain link 8`.
This is presentation ordering only. Do not reorder the registry, bodies or joints.
Use shared Lucide Camera/Light/Rigidbody/Environment/Entity glyphs from cached DPI
atlases, 20px glyphs in compact 22px logical rows, 16px indentation, 2px row gaps,
subtle selected fill and a thin blue active marker. Groups have counts and an
Editor-only tooltip. Keep search pinned above the scrolling tree; long names scroll
horizontally. Hide empty groups. Default Physics Bodies open, other groups collapsed.

The shared generation/registry-checked selection supports single and Ctrl toggle for
bodies and scene-only entities; the last selected entity is active for Properties and
the Properties panel. Gizmos affect the complete eligible selection. Retain the
4096-entity presentation selection bound, independently of Transform staging/query/history admission. Reject stale/foreign identities
before changing selection, clear on scene/reset/backend replacement, and prune destroyed
entities. Selecting in Hierarchy arms the next Viewport left click to activate and
select in one gesture without forwarding the UI gesture or stealing keyboard focus.
Other widgets, text editing, camera gestures and drag/drop retain their routing.

Search matches names while retaining genuine ancestor and category paths. A separate
filtered tree state keeps normal expansion intact when search is cleared. An external single-object Viewport click opens the active path and reveals it once.
Hierarchy selection and marquee commits preserve Hierarchy scroll and do not reveal
an offscreen anchor. Scrolling remains under user control. On a different Scene registry, reset only tree scroll to the top once,
so a long previous scene cannot hide the new scene's category headings. Reuse model
storage and icon textures; no per-frame GPU creation.
Hierarchy remains upper-left with Properties directly below; existing layout version,
Reset Layout and saved custom docking are unchanged.

Jolt ConvexHull admission uses owned validated local vertices, preserves sharp hull
geometry and authored mass/inertia/COM, and reports entity, shape and backend errors.
A failed scene/backend construction retains the previous valid world. This conversion
is not authorization to tune SphereLattice stiffness, solver, material or sleep behavior.

Profiler FPS is labeled as a rolling 120-frame wall-time average. Its tooltip
distinguishes present pacing and visible workload from GPU busy time and Physics
Step. Timing evidence must separate those quantities and preserve percentile tails.


### Owner Corrections 05: contextual authoring, group edits and startup

Camera and Light Properties edit real RenderCameraComponent/RenderLightComponent
values through deferred Update commands with Apply/Revert, finite/range validation,
identity/generation guards and authoritative readback. Camera edits preserve Editor
Camera vs Scene Camera selection. Expose perspective FOV, orthographic height and
near/far clips. Lighting supports one directional, one point and one spot light;
reject duplicate type capacity before writing. Directional range is disabled with
an explanation. Spot shadows follow the contract below. No authored fixture changes
occur on startup.

Camera/Light icons use the shared cached SVG atlas and are selectable editor overlays.
Selected cameras show their projection/clipping frustum; lights show supported range,
direction or cone guides. These are separate from game mesh geometry and can be
hidden from Viewport Display Options. Toolbar/gizmo/Alt navigation and scene drops
retain input priority. Ground/wall surface editing uses the existing shared typed
material contract; explain sharing and keep physics material parameters distinct.
World Environment exposes actual sky visibility. Ambient is the shader's fixed 0.05
factor; sky tint/exposure/rotation have no authoring API and are not fabricated.
An absent optional Visibility component means visible, as in normal extraction;
the first explicit sky toggle adds that component through the same deferred API.

Shift+click ranges use the expanded, filtered visible entity rows, excluding category
headers. Ctrl+Shift adds a range. Duplicate names remain distinct IDs. If the anchor
is hidden or stale, the clicked row becomes the new anchor. The clicked endpoint is
active. A range exceeding the 4096-entity presentation capacity is rejected as a
whole with the previous selection intact. Transform batches use the scalable resource policy below. Search clearing restores normal expansion and real parent paths.

Group gizmos default to world axes and the arithmetic mean of selected transform-root
origins. Center/Pivot/Individual Origins and World/Local options follow the scalable
authoring contract below. Selected descendants of selected ancestors inherit
exactly once. Translation/rotation convert through each actual parent world inverse;
unsupported shear, singular/mirrored parents and parented live bodies are rejected.
Scale handles control a uniform group factor (including differently rotated objects).
Keep the gizmo's raw axis matrix separate from the uniform group preview and derive
the factor from the start of the current drag, avoiding frame-to-frame feedback.
Each completed gizmo drag is one atomic transaction, automatically validated and
committed at the next Update boundary after mouse release. No mandatory Apply
button intervenes between ordinary gestures. Begin the next drag from authoritative
readback; tool/selection changes cannot cancel a released gesture or earlier commits.
Esc cancels only the current unfinished gesture and suppresses it until mouse release.
A failed gesture leaves all earlier committed work intact. No session-long transform
transaction is permitted. History retains at most 32 complete commands and 8 MiB of owned command payload;
editing can continue indefinitely as the oldest history entries retire.

Preview changes presentation only, without moving bodies. During Live Edit, physics
continues normally, including unselected objects. A running gesture has a fixed
world-space candidate and captures latest source poses at each safe preview/commit
query; no per-frame teleport, synthesized throw velocity or Dynamic-to-Kinematic
conversion occurs. Final release rechecks the latest world before atomic mutation.
Static, Dynamic and Kinematic bodies retain their type and current linear/angular
velocity; edited movable bodies wake and dependent contact/joint caches invalidate.
A kinematic edit is an explicit pose teleport, not a velocity-driven MoveKinematic
trajectory. Physics resumes from that pose using its existing velocity. Numeric
Transform fields require pause; running authoring uses the Live Edit gizmo.

Jolt scaling stages the actual replacement shape from an immutable collider reference
and its corresponding Scene scale, then uses the existing backend SetShape path at
the safe boundary. This accounts for already-scaled startup geometry without double
or cumulative scaling. Hull COM and fixed-mass inertia follow the affine second
moment of that original geometry, avoiding repeated voxel mass sampling during drag.
Explicit fixture construction and authored mass/distribution remain unchanged. Box and
ConvexHull support validated positive nonuniform scaling. Sphere/Capsule require
positive uniform scale; the editor rejects render/collider disagreement. Cylinder
presets use their existing actual ConvexHull representation, not a fabricated new
analytic collider. Preserve authored mass; recompute inertia for the new geometry.
Retain body identity, CCD, material, velocity and motion policy. Prepare all fallible
shape/wake operations before committing any selected body. Unchanged query shapes
are cached by identity, source revision and absolute scale; idle frames do not query
or reconstruct shapes. Connected scale rejects; pose edits require the complete
joint assembly and one rigid group transformation. If running joint motion makes
that impossible, reject with an instruction to pause and select the complete group.

Undo/Redo restores complete pose batches, never simulation time or old velocities.
Pause before replaying physical history. Reject a mismatched source pose after
simulation movement rather than pretending to rewind it. Metadata history retains
its existing semantics. Scene/backend replacement clears stale generation-bound
previews/history; Reset reloads authored state. Commands retain actual parent world
matrices and reject changed parent context. Preview lookup remains bounded/sorted.
Camera/light guide segments clip against the Viewport frustum.

The vendored ImGuizmo move/scale axis hit test uses the displayed screen segment.
The prior ray/plane test became undefined for an axis-normal plane through the eye,
letting a visible handle click fall through to mesh picking. Both vendored copies
retain their original license and the same narrow local correction.

Startup follows `reference/gengine-loading-screen.png`: complete dark client surface,
centered GEngine identity, restrained stage indicators and a slim blue progress bar.
Create the interactive SDL window hidden until the first complete dark GL clear/swap;
then show it normally. Reuse the existing ImGui renderer and font cache. One optional
owner-thread initialization observer presents Renderer/Physics/Scene/Editor stages,
pumps window events and redraws during cooperative CPU work. Show measured completed
body counts when available; otherwise animate an indeterminate bar. Never display a
mock percentage, sleep to extend presentation or move GL/ECS work onto unsafe threads.
Closing startup cancels construction and uses existing rollback. Initialization errors
remain in the log and receive an explicit native error dialog. No startup observer is
installed for frozen qualification/headless checks. Record elapsed time, first/stage
presentation and maximum cooperative event-pump gap in actual evidence; synchronous
driver calls remain bounded by the driver and must be disclosed if they stall.

This is one consolidated owner-corrections-05 candidate, including the loading-screen
amendment. Preserve all historical candidates. Phase 26 HUMAN ACCEPTANCE BLOCKED and
the separate SphereLattice Jolt HUMAN PHYSICS VISUAL FAIL remain unchanged. Stop for
Owner Release|x64 Rebuild Solution and visual acceptance.

### Grid, surface snapping and contact admission

The existing Viewport tools group contains one compact Snap popup; add no window.
Grid visibility and Grid Snap are independent. Persist both toggles and translation increment
(0.1/0.25/0.5/1 m presets or a finite custom positive value), Surface Snap,
Align to Surface Normal (default off), finite surface offset, Auto Resolve and
maximum correction distance (default 5 m) in existing RbsEditor settings.
World-space shared-pivot grid rounding retains group offsets. Surface contact takes
precedence along its normal; optional alignment maps the active assembly's local
+Y to that normal without changing group spacing or joint lengths.

Gizmo gestures queue candidate values and a frozen Viewport ray for the next safe
Update boundary. Query actual colliders, excluding all edited live bodies. Jolt
uses its existing cached shapes and NarrowPhaseQuery ray/shape-cast/overlap APIs;
never create a second world or move a live body to test a pose. Recheck selected
candidate pairs as well as all relevant world colliders. Use the SDK's 0.0001 m
collision query tolerance, independently of solver/contact slop. No physics tuning.
Surface casts must reach the ray-selected collider without an intervening obstacle.
Correction follows one selected contact normal, at most eight iterations and the
configured distance; conflicting contacts, invalid targets, nonfinite geometry,
query limits and uncertain clearance reject the entire proposal visibly.

Native supports grid placement with conservative actual-support separation checks;
surface casts and contact correction are explicitly unavailable. Unproved Native
clearance is rejection, not a claimed overlap result. Do not route Native through
Jolt or claim unsupported Native snapping passed. Jolt query candidates use the same validated prepared shape as scaling;
already-scaled colliders retain their actual geometry. Connected endpoints require
the full joint assembly and one rigid group move/rotation; connected scale rejects.

No queries run for an idle gizmo. Only changed scale proposals prepare geometry;
Jolt poses and repeated identical scales reuse cached shapes. Preview never changes ECS/body authoring. Direct numeric Transform fields retain
their existing validated command semantics (including supported Native mirrored
scale), with collision preflight; rejected intersections require a Viewport gizmo
for snapping/correction preview. Surface targeting belongs to a Viewport drag. Release,
Undo and Redo revalidate exact final candidates at Update
without resnapping history. Failed admission creates no history and moves no body.
Edit and Live Edit release queue a single safe commit. Queries and commits run on
the owner thread between Physics updates, never concurrently with stepping. Jolt
pose admission may preserve a pre-existing contact only when that exact collider
pair's penetration does not become deeper beyond the existing query tolerance;
new overlaps remain rejected. Scale/surface correction retains strict clearance.
Native augments cheap axes with a bounded GJK distance direction and verifies the
result using actual support planes, not an unverified GJK separation flag.
Native resting-contact pose edits require a certified no-deeper bound (complete Box
SAT, or unchanged actual geometry); unsupported contact proofs still reject. Saved
Jolt surface/resolve preferences remain inactive on Native, so they cannot block an
ordinary Native edit. All newly introduced or deeper intersections still reject.
Scene/backend replacement cancels previews and invalidates generation-bound history.
Selection changes cancel only the unfinished gesture, never an already released batch. Preserve the existing atomic body batch, mass/inertia, activation/contact
invalidation and Scene publication. Collider replacement certification is separate
and unchanged. No acceptance claim substitutes for Owner visual review.

### Permanent numeric-input contract (Corrections 05 amendment)

All editable RBS numbers use `RbsNumericInput.h` and `RbsNumericConstraints.h`,
including menu sliders, integer settings, colors and every XYZ axis. Declare bounds
from the actual Scene, Renderer or Physics contract at the call site. Missing bounds
mean finite but semantically unbounded; do not impose arbitrary world-position,
rotation, velocity or center limits. Scale restrictions depend on actual geometry and
backend: retain the existing validated safe command, including legal mirrored Native
point geometry and shape-specific positive Jolt scale restrictions. Read-only joint and unsupported
backend fields stay disabled with their existing explanations.

Drags/sliders pass the correct typed endpoints and `AlwaysClamp`, with final overshoot
protection for accelerated, rapid and outside-window motion. Open endpoints use the
next representable value. Preserve subnormal/strict-positive semantics and do not
round a valid small value to zero. A linked constraint may reject an endpoint whose
projection, inertia or product is not representable; never disguise that rejection
as a backend success. There is no new physics policy or solver parameter.

Ctrl+click, double-click and keyboard entry use a complete-token, locale-independent
parser, not scanf's accepted prefix. Accept decimal/scientific notation and an optional
leading sign; integer fields reject fractions. Reject malformed, nonfinite, overflow,
out-of-range and invalid linked proposals with a red field and readable explanation.
Enter or focus loss validates; Esc cancels. Invalid input leaves both preview and
stored draft unchanged, and Apply is blocked until correction/Revert. Values beyond
an interval are rejected when typed, rather than silently clamped. Show enough digits
to distinguish tiny positive values; text editing starts with round-trip precision.

Axis reset buttons use the same validation as typed/dragged proposals, resetting only
the clicked axis. RGB channels retain a visual picker; hidden color-edit numeric
inputs and context options must not bypass the shared parser. Avoid per-frame parsing,
texture creation, and numeric editor allocation. Shape representability checks run
only on changed proposals. Text editing is retired on selection, scene or backend
generation changes; it must not transfer an old field into a newly selected entity.
Finishing/canceling scalar text entry or accepting/rejecting a scalar drag arms the
existing one-shot Viewport activation/selection handoff. The next routed left click
can select immediately; active text, popups, drag/drop and Alt/camera guards remain.

Bounds supplement authoritative Apply/readback, never replace it. Keep collider
confirmation, overlap admission and rollback, body identity/joint safety and the
existing deferred Update boundary. Direct Transform fields preserve the existing
safe edit path and cancel outstanding unfinished group previews; group release/Cancel and
atomic Undo/Redo remain one generation-checked batch. No fixture defaults change.

Validate the common widget through actual ImGui mouse/text frames at 100/125/150/200%
scale, including endpoint/overshoot/Shift/outside-window motion, malformed/manual
entry, strict-positive and linked constraints. Retain focused Native/Jolt API and
atomic-history checks plus actual desktop screenshots. Inventory and evidence for
this amendment live under `candidates/owner-corrections-05/numeric-bounds-01/`.
The preceding Corrections 05 source/build/evidence remain historical and immutable.

### Camera / Light Editor and Spot Shadow contract

Scene icons are transparent, fixed-screen-size billboards at real ECS world anchors,
clipped to the Viewport. Default to 40 logical pixels, or 44 for Spot. Scene Icon Size
is a persisted bounded 75-200% preference, multiplied by the existing DPI scale.
Use tightly fitted SVG-derived 44/55/66/88/110/132/176 pixel atlases, selecting a
cached raster at least as large as the displayed glyph. Camera uses licensed Lucide camera; Directional
uses Lucide sun; Point uses Lucide lightbulb. Spot uses GEngine's original studio/stage
spotlight SVG: barrel housing, circular front aperture, mounting yoke and short
diverging rays. Warm yellow is shared with other light icons. Never substitute a
flashlight, generic cone, boxed text or proprietary Unity/Hazel artwork. Selection
uses a subtle blue ring without a filled button box. Reuse AssetsManager caching and
selection identity; no runtime SVG parsing, game mesh, picking-buffer mesh or shadow
caster is created for an icon.

Viewport Options persist Scene Icons, independent Camera Frustum and Light Gizmos
Off / Selected / All (Selected by default), and Spot Inner Cone / Outer Cone / Range.
These preferences affect only overlays. Cone boundaries intersect the actual radial
PBR range sphere; the range cap is separate from the 2D icon. Directional guides use
parallel arrows; Point range uses three great circles. Camera frustums use the same
rigid pose and CameraSnapshot projection/clip convention as the scene camera. Invalid
scaled camera poses must not display a misleading valid frustum.

Spot guides are generated in world space from the authoritative composed Light
Transform: position is the translation, direction is normalized local -Z, and the
cross-section axes follow the Light's local X/Y orientation, including roll. Remove
scale from this frame so authored range remains a world-space radius. Never rebuild
the cross-section from camera-facing axes or a world-up threshold; the latter ignores
roll and introduces discontinuities near vertical directions. Invalid/degenerate
frames do not emit guides. The billboard remains an independent screen-space icon.
Only projection/clipping may consume the active Viewport CameraSnapshot. Camera
orbit/pan/zoom and independent Camera Views must not alter world guide vertices.
Normal perspective/silhouette changes are expected; do not label them a rotating
world-space cone without measuring an orientation change. Preserve homogeneous
near/eye-plane clipping and all independent guide visibility preferences. Guides
remain editor overlays, including their existing visibility through scene geometry;
Camera Views/game output and shadow maps do not contain them.
Point's existing `range` is cube-shadow coverage, not a finite illumination cutoff.
Label it **Shadow range**, explain the unsupported illumination cutoff visibly, and
use that exact radius for the sphere. Preserve existing Point attenuation. Require
range above the actual point shadow near clip and a representable projection when
shadows are enabled. Spot retains its supported radial illumination cutoff.

Camera Properties identify the actual Active Scene Camera. Preview is explicit;
End Preview restores the previous view. Selection/editing never changes the Editor
Camera. Orthographic Size is half-height, converted to the existing full-height
Scene field. Camera gizmos support Translate/Rotate; Directional Rotate only; Point
Translate only; Spot Translate/Rotate. Disable other operations with explanations.
Directional Position is an editor anchor and cannot affect illumination or shadows.
Existing shared pivot, atomic Apply/Cancel and generation-checked Undo/Redo apply.
Camera/Light rotation fields read the authoritative quaternion, including authored
poses that have no synchronized Euler cache. Bind directional cascade slices to the
actual preview/active camera clip interval. Preserve the authored distribution and
exact default-view values; reject clip intervals unable to represent five distinct
float slices. A valid non-default Camera preview must not terminate rendering.

Spot Cast Shadows uses one retained perspective 2D Depth32Float target in the existing
FrameSubmission owner. World local -Z is the ray; vertical FOV is twice the outer
half-angle; aspect is 1; far clip is range; near is min(0.1 m, 1% of range). Reject
nonfinite/noninvertible projection. Capacity remains one light of each existing kind.
Moving/masked casters use existing coverage, instancing and conservative visibility.
Cache by exact relevant geometry/material, light pose/cone/range and target identity.
Camera changes alone do not dirty Spot shadows; receiver bias/filter changes do not
require a depth rerender.

Spot Properties expose Cast Shadows, resolution 1..8192 pixels subject to GPU
allocation, normalized depth bias 0..1, and Hard / 3x3 / 5x5 PCF. Defaults are 1024,
0.001 and 3x3. Use the permanent bounded numeric controls. Apply validates and prepares
a replacement target before changing the real component; allocation failure retains
the old target/component. Readback and RenderState revision comparison include every
new setting. Disabled shadows skip their pass; hiding guides never changes shadows.
Apply visibility only to the corresponding Spot PBR contribution. Directional/Point
behavior and game output remain independent of editor overlays.

Validate focused Debug/Release builds, actual GL ON/OFF and moving-caster/light cases,
settings/readback/rollback, other-light shadows, picking, scene lifecycle and real
Camera/Light interactions. Use delayed PassTiming queries with no glFinish or image
readback in timing loops. Inspect actual RBS screenshots at normal/close distances
and supported DPI scales alongside attributed Unity illustrations kept outside runtime
assets. Phase 26 HUMAN ACCEPTANCE BLOCKED and the separate Jolt SphereLattice HUMAN
PHYSICS VISUAL FAIL remain. Stop for Owner Release|x64 rebuild and visual review.

### Entity visibility, naming and independent Camera Views

The Owner-authorized `editor-views-01` follow-up permits independently dockable
Camera View windows. These are views of actual Scene Cameras, not extra feature
panels or additional scene/simulation owners. Preserve the default main layout and
all saved custom docking. Each Camera View uses an immutable `###` window ID derived
from its preset authoring identity; its visible title follows the current ECS name.
Selecting/editing a Camera or opening its window never changes the main Editor
Camera. Keep existing explicit Preview/End Preview actions.

Each visible Camera View takes an authoritative CameraSnapshot with its own pixel
extent/aspect, uses a cached color target, and submits the same Scene state through
the existing FrameSubmission. Reconfigure targets only when their description
changes. Retain per-view directional depth and exact cache state; share the existing
camera-independent Point/Spot resources. Closed, collapsed and inactive dock tabs
skip rendering after their visibility is observed. No view advances physics. Retire
stale camera identities and resources safely across deletion, scene/backend
replacement and shutdown; report invalid Camera/target errors in the affected view.

Every real Hierarchy entity has a compact Lucide eye/eye-off control and Properties
has Scene Visible. This is editor presentation only: hide its Scene View mesh/icon
and picking without modifying runtime Visibility, Camera activation, Light intensity,
shadow casting or Physics. Camera Views show runtime output, independent of this
filter. Hiding an entity does not invent parent inheritance. Gizmo Off/Selected/All
remains a separate visualization preference. History and identity remain valid when
an entity is selected from Hierarchy while hidden.

Rename is inline in Hierarchy via F2, name double-click or the entity context menu.
Select the whole text on entry; Enter queues a validated authoritative Tag name
change at Update, Esc cancels. Reject empty/whitespace-only names and control
characters with feedback. Duplicate names are legal and remain distinct entities.
Category headers cannot be renamed. Mutable names never identify handles, selection,
history, body/joint mapping or docking windows. Preserve the original authored name
separately for the existing backend-rebuild membership guard. If the selected entity
is excluded by Hierarchy search, a rename request clears that filter and reveals
the real row before focusing its text input.

Visibility and rename join the same bounded chronological history as Transform
batches. Undo/Redo checks scene generation, target identity and expected current
value. Scene/backend replacement retires stale commands. Metadata edits do not
invoke physics operations; Transform safety/atomicity remains unchanged.

As explicitly confirmed by the Owner, persistence uses the existing ImGui settings
infrastructure per ScenePreset/configuration, with no general scene serializer.
Preset construction sites assign stable numeric role IDs and semantic ordinals,
independent of runtime handles and display names. Store an authored signature before
interactive/body-state restoration; mismatched or removed entries do not apply to a
different object. Keep role IDs stable when editing a preset; changed roles require
a new identity/version. Settings retain name overrides, Scene Visible and open
Camera Views across reload/restart. Dynamic duplicates/new entities retain normal
ECS names and session Undo/Redo, but do not claim restart survival or acquire a
copied preset identity. Runtime UUIDs/handles/generations and scheduling are unchanged.

Compare three original studio Spot SVG designs at the same real Viewport pose,
size and display settings. Preserve all candidates and screenshot comparisons;
choose the default from actual readability, with no proprietary Unity assets.
The inspected follow-up selects A, the angled studio fixture, as the default in
both Viewport and Hierarchy. Saved entries containing overrides require an exact
nonempty compatibility signature; incomplete entries fail closed with feedback.
Check normal/long/close distances, all four DPI scales, overlap picking, two or more
Camera Views, docking/resizing, visibility/runtime separation, naming/history,
preset/backend replacement and clean teardown. Measure bounded render timing/draws
and allocation behavior without synchronous GPU readback in the timing loop.

### Jolt parameter authoring and controlled diagnostics

The `jolt-parameters-01` campaign exposes pinned Jolt 5.6.0 public settings inside
World Settings > Physics > Jolt Physics. Solver, Collision Response, Sleeping and
Advanced Diagnostics are collapsible sections in the existing panel. Show effective
values and SDK defaults separately. Collision substeps mean the actual
`PhysicsSystem::Update` argument; they do not change the Scene's fixed timestep.
World defaults remain 10 velocity / 2 position iterations, 1 collision step,
1 m/s restitution minimum, .2 Baumgarte, .02 m slop/speculative distance, sleeping
on with .5 s / .03 m/s, warm starting/contact cache/manifold reduction on.

World Apply is a typed, deferred owner-thread command and requires Pause. Copy the
current PhysicsSettings before changing only owned fields, then read back the
actual result. Restart affected sleep tests and invalidate cached contacts/warm
starts without replacing body/world identities. Revert discards a draft; Reset to
Preserved Baseline stages the world-entry values and still requires Apply. Main
Reset/preset load restores authored construction. Experimental overrides are not
persisted into authored presets or qualification fixtures. Backend switching keeps
the existing transactional reconstruction: incompatible Jolt overrides reject a
Native switch with the old world intact. Restore baseline before switching.

RigidBody properties retain material restitution/friction, damping, gravity factor,
CCD and per-body sleeping controls. Jolt's public MotionProperties setters support
velocity/position iteration overrides without body reconstruction. Zero inherits
the world; velocity 1 is rejected because friction needs at least 2. The maximum
contact/constraint override affects the whole island. Disable these overrides for
non-Dynamic bodies and Native. A staged body Reset restores that body's preserved
world-entry settings. Body edits retain the existing safe boundary, authoritative
readback, mass/inertia and joint protections. World restitution velocity and world
sleep thresholds are never presented as per-body parameters.

Use the shared bounded numeric widgets and exact text-entry validation. World
velocity iterations are 2..255, position 0..255, collision steps 1..16; these upper
bounds are explicit interactive resource budgets, not SDK limits or qualification
criteria. Position 0 disables position correction and is diagnostic. Baumgarte is
0..1; contact distances are finite/nonnegative with finite squares; restitution
minimum and sleep time/speed are finite/positive with a finite positive sleep
product. Never clamp away a backend rejection. Native controls remain separated.

GEngine intentionally combines contact friction and restitution by multiplication;
Jolt's SDK default combination functions differ. Preserve this integration policy.
Show measured combined coefficients only from actual observed contacts. Contact
sampling is opt-in and uses bounded preallocated, thread-safe callback storage;
report dropped records. Callbacks never mutate contact settings. Pre velocities
are measured at the first manifold point during the callback; post velocities are
at the corresponding points after the complete Update, not isolated pair impulse
measurements. Callback counts are not unique pairs, and speculative depth can be
negative. No invented solver-only timing or inferred restitution pass/fail.

Diagnostic launch profiles are explicit, bounded Sandbox-only SphereLattice/Jolt
runs. Reuse the real authored Scene and scheduler. Preserve initial-state records,
fixed camera/display settings, isolated variable changes and baseline evidence.
Keep observer overhead and repeated-baseline reproducibility distinct from solver
cost. Damping/sleep comparisons need evidence first. Experimental bouncing is not
acceptance: inspect penetration, residual motion, energy and settling as well as
visual appearance. No profile is adopted automatically. Phase 26 HUMAN ACCEPTANCE
BLOCKED and SphereLattice HUMAN PHYSICS VISUAL FAIL remain until Owner review.


### Selection correction and Viewport marquee contract

Hierarchy uses click to replace/anchor, Ctrl+click to toggle, Shift+click to select
an expanded/filtered entity-row range, and Ctrl+Shift to union that range. Category
headings are never entities. Physical vertical wheel input stays vertical while
Shift is held in Hierarchy; horizontal wheel and scrollbars remain available.
A local selection consumes its own reveal notification. It must never center the
clicked row on the next frame, reveal the offscreen anchor, or keep scrolling after
release. No per-frame focus request or range rebuilding is permitted.

Ctrl+A belongs to focused Hierarchy with no active field, rename or popup. It
selects the filtered focused category; a focused authored SphereLattice sphere
uses its authored dynamic-sphere cohort (180), excluding static room bodies.
Select All Spheres is also available from entity/category context menus and selects
the complete authored cohort regardless of filtering/collapse/editor visibility.
Cohort membership uses preset role identity, never mutable names or runtime handles.
Normal row ranges include hidden entities when their rows are present. Scene/backend
replacement and stale generation checks remain mandatory.

Ordinary selection and visible-mesh/collider highlights support 4096 entities.
Selection storage grows on demand and uses sorted stable-identity membership.
Transform authoring uses independent staging, geometry, query and history budgets.
Rejected operations preserve the complete selection. Numeric
Transform fields describe the active entity and are disabled for multi-selection;
use the existing group gizmo. Properties displays the complete selected count.

Left mouse-down on selectable Scene geometry or empty Viewport space arms a tentative
click/marquee; 5 logical pixels distinguish dragging from clicking. Preserve the
previous selection while armed. A below-threshold release selects the original
generation-checked pick (or clears an unmodified empty click); a drag does not first
select its starting object. Draw a clipped translucent blue rectangle
and border in every drag direction. Release commits, Esc/focus loss/navigation or
scene replacement cancels without changing selection. Normal replaces, Shift adds,
Ctrl toggles once per distinct entity. Toolbars, gizmos, icon clicks, text/modal input,
Alt/RMB navigation and scene drag/drop retain priority. Selecting never mutates Physics.

Default rectangle selection tests actual picking geometry/alpha coverage against
the matching picking depth. Select Through (persisted in Viewport Options) disables
depth testing for that query only. GPU rasterization handles partial intersection,
near clipping, perspective/orthographic projection and framebuffer DPI conversion.
Coverage queries issue only on release; poll availability in later frames without
waiting or reading pixels. Reuse query objects at their high-water capacity; do not
create resources or traverse rectangle candidates every idle frame. Retain stable
identities with the asynchronous result; reject stale/hidden results as a complete
set. Visible editor icon hit regions can participate separately from game geometry.
Marquee never auto-scrolls Hierarchy. Independent Camera Views remain read-only
render previews; selection is supported in the main Scene Viewport, including its
existing Scene Camera preview mode.

Visible-mesh highlights must use actual color-pass depth/stencil sample coverage
for supported opaque depth-writing geometry, including scene Helper/PointLight
roles. A selected helper must not force every selected opaque mesh into a separate
mask draw. Diagnostic lines do not inherit entity stencil writes. Preserve MSAA,
occlusion and unmodified selected PBR interiors; do not conceal artifacts with
depth bias, through-object outlines or disabled highlights. Compatible opaque and
masked instances may be grouped by active/secondary/unselected class without
changing transparent ordering, shadow batching, picking or instance transforms.

Selection performance comparisons distinguish the exact selected identity set
(180 spheres versus spheres plus room/helpers), one-time release/query work and
steady highlighting. Use identical paused state, camera, framebuffer and graphics
settings, delayed available GPU timings, CPU median/p95/p99, draw/batch/upload and
allocation counts. Include 0/1/64/65/180 and highlight-off diagnostic controls,
matched Hierarchy/Marquee sets, orthographic views and running Physics separately.
Capture runs with explicit pixel readbacks are separate from timing runs. Never
attribute a larger mixed selection's cost to the input method alone.

### Shared Physics Properties contract

Multi-selection shows total selected, bound bodies, non-physics exclusions and
unsupported counts. Show Mixed Values per field. Only fields explicitly edited
(or explicitly staged by Reset Shared to Baseline) may change. A complete batch
contains every selected body; missing identity/readback or an unsupported selected
field rejects the entire batch, never a supported subset. Preserve other material
fields, authored mass, motion type, Native sleep thresholds, transforms and velocities.

Native supports bulk Friction and Restitution. Jolt additionally supports damping,
gravity factor and iteration overrides on Dynamic bodies, and CCD/sleep enable on
moving Dynamic/Kinematic bodies. Static objects have material editing only. Explain
disabled controls. World restitution threshold, sleep thresholds and world solver
defaults remain separate. Product material combination is unchanged; per-body solver
overrides may affect the entire island.

Apply queues to the existing owner-thread safe Update boundary. Validate identities,
expected prior properties, duplicates, limits and every proposal before publication.
Prepare the complete invalidation graph before side effects. Discarding a failed
preparation rolls back the complete proposal; the subsequent publication is no-fail,
uses existing backend setters, and preserves body/world/joint identities. A backend
readback invariant failure restores every member's prior properties, verifies the
rollback and pauses simulation. Contact caches remain invalidated and bodies are
conservatively awake after that exceptional recovery; never report a failed rollback
as successful. Read every
body back after Apply and report the complete verified count. No simulation step or
observer may interleave the batch. Session-only edits never persist into authored
presets or frozen qualification fixtures. Revert discards drafts; Reset Shared to
Baseline stages each body's preserved entry values; main Reset/scene reload rebuilds
authored values. Scene/backend replacement invalidates drafts.
Capture the baseline at the first safe Update boundary even when the initial scene
generation is zero, before any interactive property command. An uninitialized
baseline must have a distinct sentinel; never capture already edited values as
the preserved baseline. Missing baselines reject the complete reset explicitly.

Bulk Physics Properties currently have no Undo/Redo history. State this visibly;
Ctrl+Z/Y remain the bounded Transform/rename/visibility history and must not be
presented as physics-property history. No default solver/material changes are approved.


### Scalable selection and group Transform authoring

General Hierarchy commands act on real ECS identities: Select All in Scene,
Select Filter Matches, Select Scene-Visible Entities, Select All of Type, Select All
Children (all genuine descendants), Invert Selection (full scene), and Deselect All.
Clearly label full-scene versus filtered/Scene-Visible scope. Ctrl+A retains focused
filtered-category scope, including the explicitly displayed SphereLattice cohort
scope when its authored sphere row is focused. Type selection is general-purpose:
Camera/Light/Mesh/Rigidbody components and authoritative Sphere/Box/Capsule/Convex
collider types, independent of names and authored preset. Full-scene/type/children
selection includes hidden entities; selection never changes visibility or Physics.
Keep virtual category headings out of selection and preserve physical Shift-wheel,
range anchors, duplicate names and the no-auto-scroll selection contract.

Selection's 4096-entity presentation budget follows the existing renderer ID and
highlight capacity; dynamically allocated ordered IDs and sorted membership avoid
fixed arrays and quadratic row membership. Reject an over-budget request as a whole
with the old selection intact. Do not equate selecting thousands with permission to
transform unsupported entities or shapes.

The Viewport editing group exposes Center / Pivot / Individual Origins and World /
Local orientation through compact controls. Center is the arithmetic mean of actual
transform roots; Pivot is the active entity's world origin. Local uses the active
entity's orthonormal world orientation. Shared translation preserves group offsets;
shared rotation rotates positions/orientations around the chosen pivot; shared
positive uniform scaling scales both offsets and each object's collider/render scale.
Individual Origins rotation/scale keeps each root's world origin fixed. Individual
World rotation uses common world axes; Individual Local rotation uses each root's
own axes. Translation remains common. Preserve local-to-parent conversion and reject
shear/singular parents. A selected descendant inherits its selected ancestor once;
Scene batch APIs reject duplicate roots and explicit parent/descendant double edits.
No permanent group Entity is created. Persist only pivot/orientation preferences.

Interactive Transform budgets are explicit resource limits, not solver or physical
acceptance thresholds: at most 4096 roots (the pinned Jolt body owner capacity),
262144 aggregate collider source points, 131072 exact narrow queries and 4194304
candidate sweep comparisons. Exceeding any budget rejects the complete operation
with an explanation and full selection retained. Buffers grow at high water, and
history is separately limited to 32 commands / 8 MiB, evicting whole oldest entries.
History payload allocation precedes publication; committing and retiring history
must not allocate. Preserve generation/expected-pose guards and running history's
pause requirement; Undo/Redo is not a rewind of Physics time.

Candidate AABBs conservatively filter exact selected-pair and Native world tests;
Jolt external queries use the existing world broadphase and constant-time selected
body exclusion. AABB overlap is never acceptance or a penetration decision. Keep
exact collider tests, existing contact tolerance and complete-assembly joint rules.
Check owner-thread/step admission once per batch, then validate every identity;
never repeat a full-world busy scan for every member. Jolt external queries exclude
fully selected broadphase layers; a fully selected world still receives the complete
candidate-pair sweep. This changes query work only, never world ordering or stepping.
Supported Jolt relative scales query immutable collider geometry directly, including
uniform group scaling of nonuniform authored Box/Hull geometry. Validate the actual
Jolt shape's scale contract; Sphere/Capsule remain positive and uniform. These previews
do not rebuild hulls or mutate live shapes on every pointer movement. Native previews reuse
owned draft shapes and immutable source geometry between pointer movements, keyed by
handle/revision/reference scale. Retire those drafts on world close and validate every
changed candidate. Native sphere contact clearance uses double-precision analytic
distance at the same contact tolerance, avoiding coordinate-sized float support errors.
Final replacements,
fixed-mass inertia, wake graph and all fallible work are prepared before publishing
any body/ECS value. Query budgets bound dense overlapping geometry, which may remain
unsupported even when thousands of separated bodies fit. No idle placement queries.
Retain existing cached preview buffers between gestures and cache idle selection
geometry until a real input/lifecycle change or simulation tick requires refresh.

Viewport toolbar backgrounds and controls composite above the shared gizmo and
component guides. At high DPI the simulation group may stack below editing tools;
its hover region must reserve new pointer presses before ImGuizmo admission. An
already captured gizmo drag retains its gesture when crossing a toolbar. Use the
existing Viewport draw list channels, without another window or rendering system.

Validate size ladders 1/64/65/180/181/256/512/1024 and feasible 4096 with Native/Jolt,
actual parent roots, general selection scopes, motion types, contacts/joints,
invalid last-member rollback and complete history. Separate preview/commit/Undo
latency, allocation and history bytes from sustained renderer/GPU work. Preserve
selection-highlight-02 batching/occlusion and every prior evidence identity. Stop
for Owner Release|x64 rebuild and manual acceptance; Phase 26 and the separate
SphereLattice physics visual failure remain blocked.


### General-purpose one-shot Rigidbody impulses

Physics / Apply Impulse belongs to contextual Properties for single and multiple
selection, not a preset-specific panel. Default: +X, World, Center of Mass, Impulse.
Provide normalized custom direction, positive/zero finite magnitude with a nonzero
linear or angular action, World/Local axes, COM/custom offset, Impulse/Velocity
Change, optional angular action, Apply Once and authoritative velocity readback.
Custom points are offsets from each body's actual COM in the selected axes, not
scaled mesh pivots. World vectors never rotate with the body or Editor Camera.
Local vectors use the body's authoritative world orientation at execution.

Impulse uses Ns (angular Nms); Velocity Change uses m/s (angular rad/s), converted
through actual mass/world inertia to physical impulses. Off-center linear actions
also produce torque. Reject nonfinite vectors, zero direction, overflow, singular
inertia and actions whose intermediate/resulting velocities exceed the existing
backend limits; never silently claim unclamped Velocity Change. All targets must be
Dynamic rigidbodies. Static/Kinematic/non-physics or stale members reject the whole
action. Connected Dynamic bodies remain eligible; leave constraints to the ordinary
solver. Shape types do not select separate impulse implementations.

An explicit button creates one immutable command for the existing safe Update
boundary. Consume it once on success or rejection. Scene/backend/selection changes
invalidate queued current-selection actions. Edit/Paused applies without stepping;
running commands precede the next ordinary step. Complete validation/allocation
precedes mutation and authoritative post-action readback verifies every target.
Retain original mass/inertia, material, shape, CCD, constraint and identity settings.
An exceptional readback invariant failure restores the complete velocity batch and
pauses for inspection; never report rollback failure as success. Physical actions
have no Transform Undo and never pretend to rewind physics time.

Use a distinct 4096-body batch budget matching current backend capacity. Keep
session-only records bounded to eight complete actions / 8 MiB, recording targets,
actual impulses/points, before/after velocities, tick, body properties and effective
world settings. Save temporary parameters and stable preset key/signature targets
in memory. Reset reloads authored conditions and does not replay. Explicit Repeat
resolves current handles from matching keys/signatures; changed/removed/transient
entities require reselection. No disk persistence or general serializer is implied.

Viewport Options exposes impulse arrows independently from transform/debug tools.
Project fixed world-space direction guides; Local follows the body's orientation.
Show units/magnitude and the selected-body count. Arrows use the existing clipped
Viewport draw list, never game/shadow geometry, and do not capture input. Direction
is adjusted with numeric/vector controls; a separate interactive direction gizmo is
optional and must not be decorative. No idle impulse queries when the overlay and
section are closed. Preserve T/R/S, picking, navigation and drag/drop priority.
Use a compact dark backdrop behind the wrapped direction/magnitude caption so it
remains readable over bright geometry. When retained records belong to an older
scene generation, label them as previous-scene actions separately from current
velocity readback; Reset never implies replay.
Usage, units, repeat identity policy and capability limits are documented in
[RBS impulse authoring and debugging](../physics/RBS_IMPULSE_DEBUGGING.md).

## Selected-entity Properties organization (Phase 26 refinement)

Use the following presentation order, based on actual selected components:

1. Entity header: authoritative name, type, selected count, existing editor-only
   visibility and rename affordances.
2. **Transform**: Position/Translation, Rotation, Scale; expanded by default.
3. **Surface Material**: assigned shared material identity, existing Base Color,
   Metallic, Roughness, Normal/AO and UV controls; expanded by default. These are
   rendering properties. Friction and restitution remain physical body properties.
4. **Geometry / Mesh**: actual mesh/submesh identity and supported rendering flags.
   Read-only fields must not imply mesh assignment or geometry editing support.
5. **Rigidbody**: existing motion/mass, velocity, contact material, damping, gravity,
   CCD, sleeping and backend override controls, with unchanged Apply/Revert/Reset.
6. **Collider**: existing shape/dimensions, preview and validated apply workflow.
7. **Constraints / Joints**: attached Distance Joint endpoints/anchors/length and
   effective readback. Descriptors remain read-only; no new joint editor is implied.
8. **Physics Actions**: existing general-purpose Apply Impulse, velocity change,
   direction/space/point/angular controls, temporary experiment and result readback.
9. **Advanced / Diagnostics**: runtime identities, effective/SDK defaults and sampled
   contact coefficients. World solver/backend/sleep controls stay in World Settings.

Camera and Light show Transform followed by their actual contextual component;
irrelevant mesh/body/action sections are absent. Static bodies retain supported
body/collider readback and disabled impulse feedback. Multi-selection shows count,
existing atomic group Transform, supported bulk Rigidbody with Mixed values and
Physics Actions. Do not show active-only material/collider drafts as common values
or expand the set of bulk-editable properties. Advanced IDs explicitly identify
the active object when multiple entities are selected.

All top-level sections use the existing `Component` header helper. Renamed headers
retain their prior explicit ImGui state IDs; headers do not push a new child ID
scope. Default-open flags only initialize absent state, never force it each frame.
Transform/Surface Material default open; other mesh/body/action/diagnostic sections
default closed; Camera/Light retain their contextual defaults. Section state remains
in the Properties window across entity changes during the session. No new section
state serializer or changed docking settings are introduced.

Reuse existing row/vector/color/numeric controls, bounds, units and tooltips.
At narrow widths and 150/200% UI scale, rows stack and Properties scrolls normally.
Keep existing drafts, queued safe-boundary transactions, exact authoritative reads,
bulk atomicity, validation messages and Undo policies. Reordering is presentation
only: no duplicate component state/setters, new ECS type, window, full-world query,
resource creation, reflection system or material/scene serializer.
