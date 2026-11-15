# RBS impulse authoring and repeatable experiments

Select real Dynamic rigidbodies in any scene, then expand **Physics / Apply Impulse**
in Properties. The section works for Sphere, Box, Capsule and supported ConvexHull
geometry, including the existing Cylinder representation. It has no preset-specific
physics path. Static, Kinematic and non-physics targets reject a mixed batch; no
eligible subset is silently used. DistanceJoint-connected Dynamic bodies remain
eligible and their ordinary constraints remain active.

Default is +X, World Space, Center of Mass, Impulse. Preset buttons choose +X/+Y/+Z;
custom direction is normalized. Negative components choose opposite directions.
Reject zero direction, nonfinite numbers and unrepresentable output. World vectors
remain fixed when the body or Editor Camera rotates. Local vectors use the body's
authoritative world orientation when the queued action executes.

Impulse magnitude is Ns and changes velocity by J/m. Velocity Change magnitude is
m/s and is converted to physical impulse with the authoritative inverse mass.
Optional angular input is Nms in Impulse mode or rad/s in Velocity Change mode;
the latter uses actual world inertia. Custom application points are offsets from
each body's COM, in the same World/Local axes, without a Transform scale factor.
Off-center linear actions add the torque r cross J in addition to optional angular
input. Current integration angular limits are 30 rad/s; operations exceeding an
intermediate or final limit reject before mutation instead of silently clamping.

Apply Once queues one command, consumed once at the existing owner-thread safe
Update boundary. Edit/Paused applies velocity without advancing physics; Resume
then shows motion. Running simulation receives the action before its next ordinary
step. A changed scene/backend or current selection cancels a queued selection action.
All identities and numerical outputs are validated before publication. Jolt uses
BodyInterface AddImpulse/AddAngularImpulse and actual velocity readback. Native
uses its existing impulse primitives. Shapes, masses, inertia, materials, CCD,
constraints, motion types and authored settings remain unchanged.

Legacy low-level mirror pose/mass/velocity writes may be pending until a Jolt Step.
The new batch rejects such a target with explicit synchronization feedback rather
than partially flushing a rejected batch. Resume to synchronize, pause and retry.
The old qualification-facing single-body impulse functions retain their established
semantics; this Editor capability does not change frozen fixtures.

Save Temporary Experiment stores parameters and target identities in memory. Reset
Scene restores authored conditions but never replays. Load Saved Parameters restores
the draft; Restore Saved Selection resolves current entities; Apply Saved Once is a
separate explicit action. After replacement, targets resolve only through matching
preset keys and authored signatures in the same scene configuration. Removed,
changed or transient entities require reselection. Body handles and mutable names
are never used as cross-generation persistence keys.

Accepted records contain the tick, actual impulses/points, entity/body identities,
initial/result velocities, body material/damping settings and effective Jolt world
settings. Compare separate actions from the same reset conditions while changing
one parameter through the existing parameter editor. Records are session-only,
bounded to eight complete actions / 8 MiB. No experiment changes an authored preset
or qualification baseline. Impulse actions have no Undo: Transform Undo cannot
rewind physical time. Exceptional backend readback failure restores all velocities
and pauses for inspection; rollback errors are never a successful application.

Viewport Options > Impulse direction arrows is editor-only. Guides show normalized
direction at selected eligible visible bodies, with magnitude/units and counts.
Their world length is illustrative, not a force or travel-distance prediction.
Change orientation through the direction fields. Arrows do not capture input,
render into game/shadow targets or replace T/R/S gizmos.

The current scene catalog has GeometryGallery, SphereDiamond, SphereLattice,
BoxStack, SphereBoxStack, DistancePendulum, DistanceChain and DistanceChainContact.
BallSocket/HingeJoint are not available presets or shared joint APIs in this build;
do not claim their behavior is validated. Future presets using supported Dynamic
rigidbodies inherit this capability without per-preset impulse code.

Phase 26 remains HUMAN ACCEPTANCE BLOCKED. The separate SphereLattice physics
quality failure is unchanged; impulse experiments are diagnostics, not approval.
