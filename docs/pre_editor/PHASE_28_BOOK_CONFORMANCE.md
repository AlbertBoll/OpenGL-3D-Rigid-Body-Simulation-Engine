# Phase 28 book conformance and authoring

This is the source-backed implementation map. Candidate qualification results
are recorded separately in the Phase 28 evidence and review. No numerical,
runtime, Editor or Owner PASS is implied by this map. F_OWNER remains PENDING.

The reference is Gregory Hodges, *Game Physics: The Rest of Your Life*, Version
1.08, Chapters 14, 17, 18, 19 and 20. The complete official PDF is frozen at
`logs/pre_editor/phase-28/design-01/reference/GamePhysicsTheRestOfYourLife-v1.08.pdf`
with SHA-256 `6237bcafd5bb648ade5a36563e1ac1599b21d02fa29553b320f7271b66da3c56`.
The accepted source scope and entry authority are recorded separately in
`active/PHASE_28_ENTRY_ACCEPTANCE_01.json`; the Phase 28-only arena disposition is
`active/PHASE_28_JOLT_ARENA_ACCEPTANCE_01.json`.

| Chapter | Required semantics | Native provider | Jolt provider | Case |
| --- | --- | --- | --- | --- |
| 14 | Coincident anchors, common axis, free signed axial rotation | AngularJointRow hinge block | Built-in HingeConstraint | hinge-basic (BasicEquivalent) |
| 17 | Quaternion hinge formulation, sign and wrap consistency | Same quaternion hinge block | Same hinge provider behind semantic frames | hinge-quaternion |
| 18 | Independent lower/upper hinge limit with unilateral reaction | Hinge limit row | BookHingeConstraint embedding native HingeConstraint with a finite-step limit guard; no motor/spring | hinge-limited; H_LIMIT/H_CONTACT |
| 19 | No-twist quaternion relation with two free angular degrees | CV relation row | Backend-local BookCvConstraint and SDK point/angular parts | cv-free; CV_FREE |
| 20 | Separate U/V component-angle bounds, independent activation | Limited CV rows | BookCvConstraint independent angular parts | cv-limited; CV_LIMIT/CV_CONTACT |

The BasicEquivalent label is deliberate. Chapter 14's common-axis manifold is
equivalently expressed by the quaternion hinge's two forbidden components.
Chapter 17 changes the formulation of that same hinge capability. These two
Editor cases use the same verified quaternion provider; they do not pretend to
be two independent solvers. Neither CV capability is a motor, ordinary point
joint or deferred Phase 30 implementation.

## Frames and exact angular equations

Use metres, kilograms, seconds and radians as the engine authoring convention.
Book +Z-up coordinates map to engine +Y-up through the proper rotation
`R(x,y,z)=(x,z,-y)`. When both local and world bases change,
`q_engine=r*q_book*inverse(r)`; anchors, axes, geometry and inertia use that same
basis. The small two-body geometry is an explicitly authored engine fixture,
not an unreported book scene. Its complete data and action schedule are frozen
in `logs/pre_editor/phase-28/design-02/acceptance-matrix.json`.

`localFrameA/B` are body-local unit quaternions. Frame X is the hinge/CV axis;
Y/Z are the two transverse axes. Given body orientations RA/RB and frames CA/CB,
the relative quaternion is `q=inverse(RA*CA)*(RB*CB)`. To establish a general
authored zero relation q0, use `CB=inverse(q0)*CA`, q0=inverse(RA0)*RB0.
The frozen identity-pose hinge frame has columns `[Z,X,Y]`, and the CV frame has
columns `[Y,X,-Z]`. Both are right-handed.

Writing q=(w,x,y,z), the hinge forbids y,z and leaves signed rotation
`2*atan2(x,w)` free. The public angle is principal, not accumulated. CV forbids
x. Its exact derivative is `x_dot=.5*(w*omega_x+z*omega_y-y*omega_z)` in world
frame A. The three component Jacobian normals are `(w,z,-y)`, `(-z,w,x)` and
`(y,-x,w)`, with matched factor-of-two residual scaling. Quaternion signs are
equivalent; Native warm-start continuity and principal readback use their
explicit separate conventions. An authored frame is never recalibrated while
reading a moving body.

Limited CV tests `abs(2*asin(y))<=halfAngleU` and
`abs(2*asin(z))<=halfAngleV` independently. The limits act as separate unilateral
rows, retiring their cached impulses on exit. Jolt's built-in SwingTwist Pyramid
uses different atan2 surfaces and a coupled swing correction. For
`q=(sqrt(1-2*.38*.38),0,.38,.38)`, the book angles are about 44.667 degrees while
the SDK Pyramid angles are about 48.512 degrees. Fixed 45-degree SDK settings
therefore cannot preserve both single-axis and two-axis book boundaries.
The custom user constraint retains the book equations while reusing Jolt's
existing supported constraint parts, ownership, island and solver hooks.

`JointState.angular.angularImpulse` reports provider row impulse scalars in
N*m*s, not a world-space torque vector. Hinge order is limit/alignment0/alignment1;
CV order is relation/U/V. `impulseAvailable` distinguishes an actual solved value
from Tick-0/reset/inactive unavailability. Body/joint handles remain world-owned
generation identities. Authoring packets store stable authored keys, never those
handles.

The pinned Jolt hinge can discard angular velocity-row lambdas while preparing
its position correction. Its angular impulse is therefore reported unavailable,
not as a reconstructed or invented world torque. Native and custom CV retain their
actual velocity-row values. Applied Jolt parameters are queried without allocation
from the actual constraint's points, frame axes and limits; Native reads its actual
owned solver descriptor. Quaternion readback compares equivalent rotations within
Float32 roundoff, including q/-q, rather than requiring identical representations.

Limited Native rows forecast activation from the current post-contact velocity
projected through the bilateral anchor/angular block. The forecast removes this
joint's cached impulses algebraically; a changed limit side retires only that
row's actual cached impulse before the existing local solve. This catches the
angular motion induced by a linear impulse at the body's centre of mass.

For limited Jolt hinges, BookHingeConstraint embeds the original SDK hinge and
registers only the wrapper with the world. The SDK continues to solve the point,
axis alignment, current violations and position corrections. While strictly
inside the authored interval, a transient SDK angular part can activate after
the point/alignment velocity solve and limit approach speed to the exact
remaining angular gap divided by the fixed step. Its unilateral sign opposes
approach; it has no motor target, altered authored boundary or extra solver
pass. Free hinges retain the direct SDK provider. Readback delegates to the
embedded hinge's actual points, axes and limits.

Jolt CV likewise selects inside predictive U/V limits after the point and
bilateral CV relation have affected velocity. Current violations retain their
ordinary warm-start path; inside predictive rows are transient for the step.
All changes preserve the existing one Native traversal and Jolt 10 velocity /
2 position iterations. Actual numerical, allocation and CPU qualification is
recorded separately; these implementation descriptions do not assert PASS.

## Authoring and qualification boundary

The ordinary existing RBS Editor loads immutable Book Reference packets or
Enhanced copies. Meaningful mass, proportional dimensions, gravity and applicable
limits are typed and bounded. Fields outside the admitted workload are visible
with their read-only or unsupported reason. Draft values do not mutate the live
world. Apply validates a complete packet, constructs at the stopped/Ready boundary
and checks actual provider readback before committing the revision. Failure keeps
the previous committed scene. Reset Simulation reconstructs committed Tick 0;
Revert Pending discards only a draft; Restore Reference Defaults stages a new
draft requiring Apply. Case/backend switches cannot silently discard dirty work.

Native uses one fixed-size local block inside its existing solver traversal;
there is no extra contact iteration or new scheduler. The Jolt adapter keeps the
existing 32 MiB temporary arena. D28 permits only the recorded Phase 28 temporary
reservations: it retains zero incremental joint-induced steady-state heap
allocation, source-derived bounds, actual occupancy/balance evidence and lifetime
safety. Capacity alone cannot establish PASS. Arena costs remain inside inclusive
Physics CPU. No Phase 26/27 numerical or arena result transfers to these fixtures.

All six gate IDs remain in the frozen matrix. The candidate handoff records
automated A-E results, exact tested inputs/products and the five real-scene
thumbnails. Owner visual and interactive acceptance remains a separate
final-candidate observation followed by explicit Phase 28 APPROVE.
