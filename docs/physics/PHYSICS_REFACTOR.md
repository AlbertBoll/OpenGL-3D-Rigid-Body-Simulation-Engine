# GEngine Physics Architecture Reconstruction

## Phase and scope

- Phase: 00 - Physics Architecture Reconstruction
- Baseline commit: `cc40080962fc7529c8d60c1e79a4f3aa9d3c9d2b`
- Branch: `physics/refactor`
- Source inspected: current workspace as of 2026-08-31
- Production physics changes in this phase: none

This document reconstructs the current physics architecture before any behavior,
correctness, performance, or ownership changes are made. Findings in
`docs/audit/CODEBASE_AUDIT.md` were treated as leads and checked against the
current source tree.

The workspace was not clean at phase entry: `.AGENTS.md` and
`docs/physics/PHYSICS_REFACTOR_PLAN.md` were already untracked. They are inputs,
not Phase 00 implementation artifacts.

## End-to-end execution path

```text
BaseApp::Run
  |  measures/clamps render-frame delta
  v
RigidBodySimulationApp::Update(frameDt)
  |  when not paused: repeats twice with frameDt / 2
  v
_Scene::Update(substepDt)
  |
  +-> PhysicsSystem::Update(substepDt)
  |     |
  |     +-> ManifoldCollector::RemoveExpired
  |     +-> gravity impulse loop
  |     +-> BroadPhase -> SweepAndPrune1D
  |     +-> static/static pair filtering
  |     +-> Collision::Intersect(bodyA, bodyB, substepDt, contact)
  |     |     +-> sphere/sphere swept test, or
  |     |     +-> ConservativeAdvance
  |     |            +-> Collision::Intersect(current poses)
  |     |            |     +-> sphere/sphere overlap, or
  |     |            |     +-> GJK_DoesIntersect
  |     |            |            +-> support mapping
  |     |            |            +-> simplex signed-volume reduction
  |     |            |            +-> EPA_Expand for witness/contact points
  |     |            +-> GJK_ClosestPoints when separated
  |     |            +-> temporary forward/backward body integration
  |     +-> zero-TOI contacts -> persistent manifolds
  |     +-> positive-TOI contacts -> sorted transient contact list
  |     +-> manifold PreSolve -> warm start -> one Solve pass
  |     +-> chronological integration to each positive TOI
  |     +-> Collision::ResolveContact ballistic/friction impulse
  |     +-> integrate all bodies through remaining substep time
  |
  +-> copy dynamic PhysicsBody position/orientation to ECS transform
        |
        v
      render passes consume Transform3DComponent
```

The `PhysicsWorld` is a state container. It does not implement the step itself;
`PhysicsSystem::Update` orchestrates the complete simulation step over the
world's body vector.

## Application and frame scheduling

### Runtime construction

`RigidBodySimulationApp::Initialize` in
`RigidBodySimulation/src/RigidBodySimulation.cpp` creates an `_Scene`, populates
ECS entities and components, then calls `_Scene::OnRuntimeStart`.

The currently active fixture setup contains:

- 64 dynamic sphere entities from a `4 x 4 x 4` nested loop;
- 1 static floor box;
- 4 static wall boxes;
- render-only light, grid, axis, and skybox entities.

Several single-body, convex-body, and box-stack fixtures are present only as
commented code and do not participate in the current runtime.

`_Scene::OnPhysics3DStart` scans every `RigidBody3DComponent`, allocates one
runtime `RigidBody3D`, creates a sphere, box, or convex shape from the matching
fixture component, copies fixture properties into the body, stores the body in
the world, and writes the non-owning runtime pointer back into the component.

### Frame and substep frequency

`BaseApp::Run` computes elapsed microseconds, pads short frames to 16 ms, clamps
long frames to 33 ms, converts the result to seconds, and calls the application
`Update` once per non-minimized render-loop iteration.

When the demo is not paused, `RigidBodySimulationApp::Update` calls
`_Scene::Update(frameDt / 2)` twice. Therefore:

- physics runs twice per rendered frame;
- gravity, broadphase, narrowphase, manifold solving, and integration each run
  once per physics substep;
- ECS transform synchronization also runs twice per rendered frame;
- render submission runs once after both substeps;
- this is two variable/clipped substeps, not a fixed-timestep accumulator.

The pause flag skips both physics substeps. There is no independent physics
clock, retained remainder, sleeping update, or island schedule.

## Detailed simulation step

### 1. Persistent-contact expiry

`PhysicsSystem::Update` first calls `ManifoldCollector::RemoveExpired`.
Each manifold reconstructs its local anchors in world space and removes contacts
whose tangential drift reaches 0.02 or whose penetration test no longer passes.
Empty manifolds are erased from the collector vector.

Mutations:

- compacts each manifold's fixed contact and constraint arrays;
- erases empty entries from `ManifoldCollector::m_Manifolds`;
- preserves cached constraint impulses for retained contacts.

### 2. Gravity

The system traverses every body, computes `mass = 1 / inverseMass`, constructs
the impulse `gravity * mass * dt`, and calls `ApplyImpulseLinear`.
`RigidBody3D::ApplyImpulseLinear` ignores bodies whose `BodyType` is `Static`;
all other types receive `impulse * inverseMass` in linear velocity.

The default world gravity is `(0, -12, 0)`. There is no general force
accumulator; gravity is applied directly as a per-substep impulse.

Mutation: dynamic and kinematic body linear velocities.

### 3. Broadphase

`BroadPhase` calls `SweepAndPrune1D` for the complete body vector.

For each body, `SortBodiesBounds`:

1. calls the shape's world-space `GetBounds(position, orientation)`;
2. expands the AABB by `linearVelocity * dt`;
3. expands it by a fixed 0.01 margin;
4. projects the min and max corners onto normalized `(1, 1, 1)`;
5. writes two endpoints into an `alloca`-allocated `2 * bodyCount` array.

The endpoint array is sorted from scratch with `qsort`. `BuildPairs` scans
forward from each minimum endpoint until that body's maximum endpoint and emits
every encountered minimum as a `collisionPair_t`.

Important current semantics:

- the broadphase is one-dimensional projected sweep and prune;
- AABB overlap on all three axes is not rechecked here;
- static/static pairs are emitted and rejected later;
- there are no collision layers, masks, sleeping filters, or persisted
  endpoints;
- endpoint storage is stack allocated every substep;
- the comparator returns `1` for equal keys rather than `0`.

Mutation: the local candidate-pair vector only. Bodies and shapes are read.

### 4. Pair filtering

`PhysicsSystem::Update` maps each candidate's integer indices back to the
world's body vector. Its only explicit pair filter skips pairs where both body
types are `Static`.

There is no entity-ID filter, self-pair filter outside SAP construction,
collision layer/mask test, kinematic policy, trigger policy, or sleep-state
test.

### 5. Narrowphase dispatch

`Collision::Intersect(bodyA, bodyB, dt, contact)` selects:

- sphere/sphere: `SphereSphereDynamic`, a swept relative-motion ray/sphere
  calculation;
- every other shape combination: `ConservativeAdvance`, which repeatedly uses
  current-pose overlap/closest-point queries and advances both live bodies.

Current-pose `Collision::Intersect(bodyA, bodyB, contact)` selects:

- sphere/sphere: direct radius overlap and analytic surface points;
- other pairs: biased GJK intersection followed by EPA contact generation;
- separated non-sphere pairs: `GJK_ClosestPoints` to provide a separation
  direction and distance for conservative advancement.

The shape type enum contains `Sphere`, `Box`, and `Convex`. There are no
specialized sphere/box or box/box algorithms.

### 6. GJK support and simplex processing

`GJK.cpp` defines an internal `point_t` containing:

- `xyz`: Minkowski-difference support point;
- `ptA`: witness point on body A;
- `ptB`: witness point on body B.

`Support` normalizes the search direction, queries A in that direction, queries
B in the negated direction, and stores `ptA - ptB`.

Shape support behavior:

- sphere: returns `position + direction * (radius + bias)`;
- box: transforms all 8 stored corners into world space and linearly scans them;
- convex: transforms and linearly scans every hull point;
- box/convex add a normalized-direction bias to the selected point.

GJK keeps a four-element stack simplex and an explicit point count. Signed
volume projection calculates barycentric weights and a new direction for line,
triangle, or tetrahedron simplexes. Zero-weight points are compacted. Duplicate
support and non-improving squared distance terminate the search. The loops have
no explicit iteration cap or finite-value check.

For an intersection, GJK expands a lower-dimensional simplex to a tetrahedron,
applies the contact bias, then invokes EPA. For a separation query,
`GJK_ClosestPoints` reconstructs witness points using the final barycentric
weights.

### 7. EPA and contact generation

`EPA_Expand` copies the tetrahedron into heap-backed `points`, `triangles`, and
`danglingEdges` vectors. It repeatedly:

1. finds the triangle closest to the origin;
2. requests a support point along that face normal;
3. stops on a duplicate or non-expanding support point;
4. removes faces visible from the new point;
5. finds boundary edges with nested triangle/edge scans;
6. closes the hole with new triangles.

It then projects the origin onto the closest face, computes barycentric
coordinates, and reconstructs world-space witness points on A and B. Those
points become the contact normal, world/local anchors, and negative separation
in `Collision::Intersect`.

EPA has no explicit iteration, point, or triangle limit. Its `HasPoint` helper
takes the triangle vector by value, causing an additional copy in the expansion
loop.

### 8. Contact routing and manifolds

The system uses two contact paths:

- `timeOfImpact == 0`: add to `ManifoldCollector` for persistent constraint
  solving;
- `timeOfImpact > 0`: append to a static transient `std::vector<contact_t>` for
  chronological ballistic resolution later in the substep.

The transient vector calls `reserve(bodyCount * bodyCount)` every update. It is
static, so capacity persists, and `clear` runs at the end of the step. Contacts
are sorted by time of impact with `qsort` when more than one exists.

`ManifoldCollector::AddContact` linearly searches for an unordered body pair.
Each `Manifold` stores at most four contacts and four
`ConstraintPenetration` objects in fixed arrays. Near-duplicate contacts are
ignored. When full, a local-space distance heuristic may replace one contact.

Mutation:

- persistent manifold/contact/constraint state;
- transient contact vector size and capacity;
- no body state is changed merely by adding a contact, except that conservative
  queries have already temporarily mutated and unwound poses.

### 9. Constraint preparation and solving

For each retained contact, `ConstraintPenetration::PreSolve`:

- reconstructs world anchors and lever arms;
- derives a world normal and two orthogonal friction axes;
- fills a `3 x 12` Jacobian;
- applies the previous cached lambda as a warm-start impulse;
- computes Baumgarte stabilization as `0.25 * min(0, C + 0.02) / dt`.

`ConstraintPenetration::Solve` constructs a `12 x 12` inverse-mass matrix,
forms `J W J^T`, obtains the generalized velocities, and calls the custom
three-variable `LCP_GaussSeidel`. It accumulates and clamps the normal lambda,
limits two friction lambdas, and applies the delta impulse to both bodies.

`PhysicsSystem` currently performs one outer manifold solve pass per substep.
The custom `Vec<N>` and `Mat<M,N>` types use `std::vector` internally, so even
fixed-size solver values and intermediate matrices are heap-backed. The
manifold `PostSolve` call is disabled.

Mutation: body linear/angular velocities and each contact constraint's cached
lambda.

### 10. Time-of-impact resolution and integration

Positive-TOI contacts are processed in sorted order. Before each contact, every
body is integrated from the accumulated time to that contact's time. The
contact then receives an elasticity impulse, a friction impulse, and (only for
zero-TOI contacts passed directly to `ResolveContact`) positional projection.
After the list, every body is integrated through the remaining substep time.

`RigidBody3D::Update` mutates:

- position from linear velocity;
- angular velocity from the gyroscopic term;
- orientation from an angle-axis delta quaternion followed by normalization;
- position again to preserve the center-of-mass relationship after rotation.

Static bodies are not excluded from `Update`; they normally remain unchanged
because their velocities are zero, but the integration path still executes.

Conservative advancement and sphere CCD also call `RigidBody3D::Update` on the
two queried bodies, then call it with negative time to unwind the query. These
are mutation-based prediction boundaries and are not mathematically guaranteed
to restore bit-identical state.

### 11. ECS transform synchronization

After `PhysicsSystem::Update`, `_Scene::Update` iterates the EnTT view of
`RigidBody3DComponent` and `Transform3DComponent`. For every non-static body it
copies:

- `RuntimeBody->m_Position` to transform translation;
- `RuntimeBody->m_Orientation` to transform rotation.

The synchronization is physics-to-ECS only during stepping. ECS translation or
rotation changes after runtime-body construction are not generally copied back
to the body. Transform scale changes are connected to shape scale handlers,
which directly rebuild or resize the allocated runtime shape.

Because scene update runs twice, render transforms are written after each
substep; rendering observes the state after the second copy.

## Files and responsibilities

### Entry, ECS, and scheduling

| File | Current physics responsibility |
|---|---|
| `GEngine/src/Core/BaseApp.cpp` | Render-loop timing, delta padding/clamp, one application update per visible frame. |
| `RigidBodySimulation/src/RigidBodySimulation.cpp` | Creates the active fixture scene, starts physics, applies pause control, requests two physics substeps per frame. |
| `RigidBodySimulation/include/RigidBodySimulation.h` | Owns scene references and pause/UI state used by scheduling. |
| `GEngine/include/GEngine/Component/Component.h` | Defines `BodyType`, fixture properties/components, and `RigidBody3DComponent::RuntimeBody`. |
| `GEngine/include/GEngine/Scene/_Scene.h` | Declares the scene registry and raw `PhysicsSystem*`. |
| `GEngine/src/Scene/_Scene.cpp` | Creates/destroys the system, constructs runtime worlds/bodies/shapes, steps physics, and copies body poses to ECS transforms. |

### Physics core

| File | Classes/functions and responsibility |
|---|---|
| `Physics/PhysicsWorld.h/.cpp` | `PhysicsWorld`; gravity and owning raw-pointer body vector; body creation/removal/destruction. |
| `Physics/PhysicsBody.h/.cpp` | `RigidBody3D`; mutable pose/velocity/material/mass/shape state, space conversion, inertia, impulses, integration. |
| `Physics/PhysicsSystem.h/.cpp` | `PhysicsSystem::Update`; full step orchestration. `Collision` namespace; sphere tests, GJK dispatch, conservative advancement, contact resolution. |
| `Physics/Broadphase.h/.cpp` | `collisionPair_t`, endpoint sort, pair construction, projected sweep and prune. |
| `Physics/Bounds.h/.cpp` | `Bounds`; AABB construction, expansion, and intersection utility. |
| `Physics/Contact.h/.cpp` | `contact_t`; body pointers, local/world anchors, normal, separation, TOI. Implementation file is currently empty beyond includes. |
| `Physics/GJK.h/.cpp` | Signed-volume simplex reduction, support queries, GJK intersection/closest points, and EPA expansion/contact witnesses. |
| `Physics/Manifold.h/.cpp` | `Manifold` fixed contact cache and `ManifoldCollector` persistent body-pair collection. |
| `Physics/Constraints/Constraint.h/.cpp` | Base constraint helpers, generalized velocities/inverse mass, impulse application. |
| `Physics/Constraints/ConstraintPenetration.h/.cpp` | Contact normal/friction Jacobian, warm start, Baumgarte term, LCP solve. |

### Shapes

| File | Classes/functions and responsibility |
|---|---|
| `Physics/Shape.h/.cpp` | Abstract `PhysicalShape`, type, source mesh points, center of mass, scale rebuild, support/bounds/inertia interface. |
| `Physics/ShapeSphere.h/.cpp` | Radius, analytic bounds/support/inertia, multiplicative scale handling. |
| `Physics/ShapeBox.h/.cpp` | Eight corner points, local bounds/center, vertex support, rotated bounds, inertia, angular projected speed. |
| `Physics/ShapeConvex.h/.cpp` | Convex hull construction, sampled mass properties, hull-point support, rotated bounds, angular projected speed. |

All physics implementation `.cpp` files below `GEngine/include/GEngine/Physics`
are compiled because `premake5.lua` includes `GEngine/include/**.cpp` in the
`GEngine` static-library target.

## Object ownership and lifetime

| Object/data | Allocated by | Stored by | Destroyed by | Current contract/gap |
|---|---|---|---|---|
| `_Scene` | `CreateRefPtr<_Scene>` in the app | `m_EditorScene`, `m_ActiveScene` shared references | Shared-reference teardown | App owns scene lifetime through shared pointers. |
| `PhysicsSystem` | `_Scene` constructor with `new` | `_Scene::m_PhysicsSystem` raw pointer | `_Scene` destructor with `delete` | Scene is the intended unique owner, but ownership is not encoded. |
| `PhysicsWorld` | `_Scene::OnPhysics3DStart` with `new` | `PhysicsSystem::m_PhysicsWorld` raw pointer | `PhysicsSystem::OnExit` or destructor | System behaves as owner; `SetPhysicsWorld` does not delete an existing world, so repeated start leaks it. |
| `RigidBody3D` | `PhysicsWorld::CreateRigidBody3D` with `new` | world `vector<RigidBody3D*>`; ECS component observes it | world destructor or `RemoveRigidBody3D` | World owns bodies; component pointer is non-owning and can dangle after stop/removal. |
| `PhysicalShape` | `_Scene::OnPhysics3DStart` with `new` | `RigidBody3D::m_Shape` raw pointer | no path found | Shape ownership is undefined; body/world destruction leaks shapes. |
| fixture properties | ECS component values | EnTT registry | registry/component teardown | Authoring-time values are copied into runtime bodies only at physics start. |
| collision pairs | each system update | local `std::vector` | end of update | Capacity is not explicitly reused across calls. |
| transient contacts | function-static vector | `PhysicsSystem::Update` | process lifetime; elements cleared each step | Capacity persists; reserve target is `bodyCount^2`. Contains positive-TOI contacts. |
| manifolds | `PhysicsSystem` value member | `ManifoldCollector::m_Manifolds` | system teardown/explicit clear | Persistent across substeps; contacts store raw body pointers. |
| simplex | GJK function stack | four `point_t` array | function return | Fixed capacity, no heap allocation in GJK simplex itself. |
| EPA polytope | each EPA call | three local vectors | function return | Heap-backed and repeatedly grown/erased during collision generation. |
| solver matrices/vectors | per contact solve | local custom `Mat`/`Vec` values | scope exit | Fixed logical dimensions but heap-backed internally. |

`_Scene::OnRuntimeStop` is empty even though `_Scene::OnPhysics3DStop` exists.
Normal scene destruction deletes the `PhysicsSystem`, whose destructor deletes
the current world. Scale-change signal connections target allocated shapes; no
explicit disconnect/lifetime contract was found in the physics teardown path.

## Core data structures

| Structure | Layout and use |
|---|---|
| `vector<RigidBody3D*>` | Dense world body array; indices are used directly by broadphase pairs. |
| `RigidBody3D` | AoS public fields for pose, velocities, inverse mass, material coefficients, type, and raw shape pointer. |
| `Bounds` | Two `Vec3f` values (`mins`, `maxs`). |
| `collisionPair_t` | Two integer indices; equality treats `(a,b)` and `(b,a)` as equal. |
| `contact_t` | Two world anchors, two local anchors, normal, separation, TOI, two raw body pointers. |
| `vector<Manifold>` | Persistent, linearly searched body-pair cache. |
| `Manifold` | Up to four inline contacts and four inline penetration constraints. |
| `ConstraintPenetration` | Body/anchor state plus heap-backed `Mat<3,12>` Jacobian and `Vec<3>` cached lambda. |
| GJK `point_t[4]` | Fixed stack simplex with Minkowski and original-shape witness points. |
| EPA vectors | Dynamic polytope points, triangle indices, and boundary edges. |
| shape point vectors | Eight points for a box; hull-size-dependent points for convex shapes; source mesh points also retained by the base class. |

There are no body handles/generations, body-to-entity back-reference, collision
layers, broadphase proxy objects, sleeping flags, island graphs, force
accumulators, or thread-local physics work queues.

## Hot loops and allocation sites

The following are architectural hot-path candidates based on nesting and update
frequency. Phase 00 does not claim measured percentages.

| Area | Frequency and scaling | Work/allocation behavior |
|---|---|---|
| Gravity | Every body, every substep | One loop; recomputes mass and applies an impulse. |
| World bounds | Every body, every substep | Shape bounds recomputed; box/convex rotate eight AABB corners. |
| Endpoint sort | Every substep | Rebuilds `2N` stack endpoints and `qsort`s from scratch. |
| Pair generation | Every substep | Nested scan; worst case quadratic candidate output. |
| Narrowphase | Every surviving pair, every substep | Static/static rejection occurs only after candidate generation. |
| GJK support | Every GJK iteration | Normalization plus virtual support calls; box/convex transform and scan points. |
| GJK simplex | Every GJK iteration | Signed-volume projections and compaction; no explicit iteration cap. |
| EPA | Every intersecting non-sphere pair | Dynamic vectors, linear closest-face scans, erase operations, nested dangling-edge search. |
| Conservative advancement | Every non-sphere candidate | Up to 10 advances; each can run overlap GJK or closest-points GJK and integrate/unwind two bodies. |
| Manifold lookup | Every zero-TOI collision | Linear scan across manifolds and up to four contacts. |
| Constraint pre-solve/solve | Every retained contact, every substep | Repeated heap-backed matrix/vector construction and multiplication. |
| TOI integration | For every positive-TOI contact | Integrates every body before resolving the next contact. |
| Final integration | Every body, every substep | Recomputes center of mass, orientation matrices, inertia, inverse, and quaternion update. |
| ECS copy-back | Every dynamic ECS body, every substep | EnTT view and two transform setters; currently twice per rendered frame. |

## Mutation boundaries

### Intended state mutation

- `_Scene::OnPhysics3DStart`: allocates runtime world/body/shape objects and
  writes `RigidBody3DComponent::RuntimeBody`.
- `PhysicsSystem::Update`: owns the step-level ordering and mutates manifold
  state, velocities, poses, and transient work containers.
- `ConstraintPenetration::PreSolve/Solve`: mutates cached impulses and body
  velocities.
- `Collision::ResolveContact`: mutates linear/angular velocities and may mutate
  positions for zero-time projection.
- `RigidBody3D::Update`: mutates position, angular velocity, and orientation.
- `_Scene::Update`: mutates ECS transforms from runtime body poses.
- transform scale callbacks: mutate/rebuild runtime shape geometry.

### Query functions that currently mutate

- `Collision::SphereSphereIntersect` temporarily integrates both bodies to TOI
  and integrates them backward.
- `Collision::ConservativeAdvance` repeatedly integrates both bodies forward,
  then integrates backward before returning.
- The const-qualified GJK and shape support/bounds paths read body/shape state
  and do not intentionally mutate it.

### Read-only boundaries

- broadphase reads bodies and shapes and writes only local endpoint/pair data;
- GJK support and simplex processing read bodies/shapes and write local simplex;
- EPA reads bodies/shapes and writes local polytope/witness data;
- rendering reads the synchronized ECS transform, not the runtime body directly.

## Verified audit findings relevant to later physics phases

These findings remain present in the current source. They are documented here,
not corrected in Phase 00.

| Audit item | Current-source verification |
|---|---|
| GE-006, degenerate collision NaNs | Coincident sphere and GJK/contact paths still normalize potentially zero vectors; solver divisions still lack epsilon policy. |
| GE-007, transform convention risk | `RigidBody3D` and some box bounds/constraint paths still transpose quaternion matrices while shape support/render composition use non-transposed matrices. |
| GE-008, timestep architecture | The render loop still pads/clamps variable delta, and the demo still splits it into two substeps without an accumulator. |
| GE-019, broadphase/capacity | `_alloca(2N)`, full endpoint rebuild/sort, equal-key comparator behavior, nested pair scan, and `N^2` contact reserve remain. |
| GE-020, ownership | World/body/shape raw ownership and empty runtime-stop path remain as described. |
| Solver allocation/numerics | Custom fixed-dimension `Mat`/`Vec` types still store `std::vector`; Baumgarte and LCP divisions remain unguarded. |
| Prediction mutation | Collision prediction still integrates live bodies forward and backward. |

Additional observations needed for later phases:

- static bodies with `inverseMass == 0` still participate in the mass reciprocal
  expression before `ApplyImpulseLinear` returns;
- `BodyType::Kinematic` is not given separate impulse/integration behavior and
  currently follows the non-static path;
- GJK and EPA lack explicit iteration limits and finite-state validation;
- body transforms, world inverse inertia, and world bounds are recalculated on
  demand rather than cached;
- no permanent physics profiling counters or automated physics tests exist.

## Validation and measurement status for Phase 00

Phase 00 changes documentation only and deliberately adds no benchmark target,
instrumentation, tests, or production behavior. The repository has no existing
automated test target and no repeatable physics benchmark target. Build and
runtime validation results are recorded in
`docs/physics/review/PHASE_00_REVIEW.md`.

Because there is no before/after implementation change:

- benchmark before: no Phase 00 performance baseline available;
- benchmark after: not applicable (identical production source);
- percentage improvement: not applicable and no performance claim is made.

Creating instrumentation and a representative multi-body benchmark is reserved
for Phase 01 by the master plan.

## Phase 00 boundary

Architecture reconstruction is complete when this document and its review
package have passed Debug/Release build validation and human review. No finding
above authorizes a correction before the corresponding later phase is approved
and started.
