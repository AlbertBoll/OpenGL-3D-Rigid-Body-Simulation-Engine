# Physics, Scene and presentation Transform contract

Scene owns one fixed-step clock. The default is 1/60 second, with the existing
pre-start 1/120 option, two steps per update and a 0.25-second pending-time cap.
Pause/resume snaps presentation history; overflow is reported rather than hidden.
Rendering never advances this clock or writes a body pose.

| Value | Owner |
| --- | --- |
| Last explicit authored pose | Scene runtime binding; updated by accepted authoring commands or a consumed legacy pose edit. |
| Current simulation pose | World-owned body. Static follows authoring; Kinematic retains prescribed velocity; Dynamic follows the existing solver. |
| Previous simulation pose | Scene snapshot immediately before each fixed tick. |
| Component pose while running | Compatibility mirror of current simulation after an update, never an interpolated pose. Simulation publication emits no authoring notification. |
| Presentation pose | Existing RenderState interpolation operands and evaluation, read without changing Physics. |
| RenderFrame pose | Immutable sampled value used by rendering, shadows and picking. |

`GetBodyBinding` returns the existing typed world handle. `ReadBodyTransform`
returns authored, previous, current and presentation values. Neither returns a
solver pointer. Stale/foreign bindings are rejected. Stop, deletion and removal
of a body or Transform component retire the runtime connection; copies retain
the existing authored/component-copy behavior without copying a live binding.

`SetLocalPose`, `SetLocalTranslation` and `SetLocalRotation` deliberately teleport
a live body, preserving velocity and using the existing normalization, wake and
contact-invalidation path. Accepted discontinuities reset interpolation endpoints.
World-space body anchors preserve their existing hierarchy behavior; non-Physics
entities continue to compose local transforms normally.

`SetLocalScale` stages and validates a live collider before committing it and the
Scene Transform. Rejection preserves geometry, ownership, handle, pose/history,
revisions, contact/wake state and notifications. Accepted changes emit the existing
committed authoring notification after storage. Notification delivery errors do
not roll back an already committed edit. Exact Transform no-ops do not replay or
repair legacy writes.

| Shape | Existing base and supported scaling |
| --- | --- |
| Sphere | Initial fixture radius; subsequent positive resulting radius uses scale X only. Y/Z do not create an ellipsoid. Explicit radius replacement establishes a new base. |
| Box | Initial scaled fixture points become its stored base. Subsequent signed/nonuniform scaling is absolute relative to that base. |
| Convex hull | Initial explicit local points, without initial Transform scale; subsequent signed/nonuniform scaling uses that base. |
| Capsule | Description radius/half-length base; initial and later scaling must be positive and uniform. |

Typed Scene edits reject nonfinite, degenerate, unsupported or unrepresentable
results. Component `SetScale` remains the separate void compatibility operation:
it emits its pre-store hook and stores Transform even if the collider rejects.
Direct field assignments do not invoke that hook. The Scene path suppresses only
its own already-committed body callback to prevent double scaling. Arbitrary
legacy callback side effects are not an atomic transaction API.

`ReplaceBodyCollider` explicitly supplies a unit-scale semantic shape description.
It stages the new world-owned collider, preserves body handle/pose/velocity, and
invalidates affected caches, contacts and wake state only after acceptance. This
is a runtime edit; it does not rewrite startup fixtures or visual meshes. The next
changed scale command applies to the replacement's new base.

The RBS collider-switching UI defaults to **Fit current mesh**. Only the explicit
replacement action borrows the current validated CPU mesh and applies its current
Transform scale to the fit. Box uses the mesh bounds and offset center. Sphere
and local-Y Capsule enclose the scaled mesh around the existing body pivot;
off-center meshes may therefore leave empty space inside those two primitives.
The body pose, mesh pivot and rotation are not moved to tighten a fit. Cached
bounds serve the centered uniform Sphere path and spherical Capsule case; other
Sphere/Capsule fits read immutable vertices without a temporary mesh allocation.
No fitting runs during ordinary Physics updates, extraction or rendering.

This is a one-shot explicit base replacement: current mesh scale is baked once
into the description passed to the unchanged unit-scale API. Later scale edits
retain the existing rules above and do not refit automatically. Turning fitting
off uses the explicitly authored dimensions. The underlying Scene/Physics API,
startup fixtures and ordinary shape creation never infer geometry from a mesh.

Mesh regeneration is independent. Bake retains its typed rejection while a live
scale binding exists, before resetting Transform or changing the mesh. Stopped,
unbound Bake retains its explicit existing Transform reset.

In RBS, click a body and use **Body Transform** for teleport, scale, velocity and
explicit collider replacement. Invalid edits show a recoverable rejection. Cyan
debug geometry is authoritative Physics; magenta is the selected presentation
outline. Interpolation can legitimately separate these two poses. The magenta
outline uses the same presentation operands as the immediately frozen frame.

For owner inspection, compare movement and pause/resume with interpolation on/off;
teleport and scale a selected body; reject zero/unsupported scales; explicitly
replace a collider and observe independent visual geometry; verify picking,
shadows and the presentation outline; then close RBS cleanly. This phase makes no
new solver convergence, Phase21 stability or joint/ragdoll claim.
# Selected mesh pixel outline (accepted Phase 24 revision)

The gold selection outline consumes the selected entity's visible render mesh
from the current immutable RenderFrame. Its mask uses the same submitted world
matrices, material coverage and scene depth; collider geometry does not define
the gold silhouette. Cyan remains authoritative Physics geometry and magenta
remains the presentation collider diagnostic. Their visibility does not control
mesh selection. Empty picks, stale identities and scene replacement clear the
selection through the existing RBS identity route.

The renderer owns a lazily allocated, bounded mask/depth region and composites
the outline before the existing multisample color resolve. Empty selection
submits no mask, copy, resolve or composite work. Scratch capacity may remain
after deselection and is reported independently from per-frame work. No
highlight operation reads back the GPU, changes a shared material, publishes a
resource, samples a different pose, or writes/advances Physics.

Qualification status belongs to the current revision handoff. The accepted
amendment requires actual highlighted RBS functional checks and matched Release
A/B non-regression evidence; this contract is not a claim those gates passed.
