# GEngine Physics Optimization & Refactor
---
# 1. Purpose

Physics Optimization & Refactor is the authoritative implementation for the GEngine physics optimization and refactor project.


The project will be executed incrementally, one phase at a time.

The goals are:

* improve physics mathematical correctness;
* improve rigid-body scalability;
* optimize GJK and all other physics subsystems;
* eliminate unnecessary allocations and repeated calculations;
* improve deterministic simulation behavior;
* improve ownership and lifetime safety;
* prepare the physics system for future parallel execution;
* preserve current intended collision behavior unless a behavior is proven incorrect.

Correctness has priority over performance.

Measured performance has priority over speculative optimization.

---

# 2. Git Safety Protocol

## 2.1 Dedicated Physics Refactor Branch

Physics refactoring should be performed on a dedicated branch:

```text
physics/refactor
```

Prefer performing the work in a dedicated Git worktree so unrelated renderer/editor work is isolated.

Example layout:

```text
C:\dev\GEngine
    existing development work

C:\dev\GEngine-physics
    physics/refactor
```

Do not overwrite unrelated working-tree changes.



## 2.2 Phase Checkpoint Rule

Every APPROVED phase gets exactly one checkpoint commit.

Example:

```text
physics: phase 00 establish baseline
physics: phase 01 add physics profiling
physics: phase 02 fix math robustness
physics: phase 03 refactor body derived data
physics: phase 04 optimize broadphase
physics: phase 05 optimize GJK core
```

The latest approved commit is always the rollback point.

At the start of each new phase:

```text
git status --short
```

must show no unreviewed changes from the previous phase.

---

# 3. Performance Measurement Rules

No optimization is considered successful without measurement.

For physics performance changes record, where applicable:

```text
Rigid body count
Dynamic body count
Active body count
Sleeping body count

Candidate pair count

GJK calls
GJK total time
Average GJK iterations
Maximum GJK iterations
Support calls
Support time
EPA calls
EPA time

Contact count
Manifold count

Solver constraints
Solver iterations
Solver time

Integration time

PhysicsWorld total time

Frame time / FPS when useful
```

Use representative rigid-body counts such as:

```text
50
100
200
500
1000
2000
```

Do not claim percentage improvements without comparable before/after conditions.

---

# 4. Permanent Tests vs Temporary Profiling

Permanent repository additions should include:

```text
Physics regression tests
Physics math tests
GJK tests
Collision tests
Physics benchmark targets
Low-overhead optional profiling counters
```

Temporary instrumentation may be used during investigation.

Temporary instrumentation must be removed before Phase approval unless explicitly useful long-term.

Do not leave:

```text
std::cout inside GJK loops
printf inside physics loops
high-resolution clock calls around every support operation
temporary experimental branches
```

inside production hot paths.

Permanent profiling should be guarded by something similar to:

```cpp
GE_ENABLE_PHYSICS_PROFILING
```

Release builds should compile expensive instrumentation out.

---

# 5. Phase 00 — Physics Architecture Reconstruction

## Objective

Fully understand the existing physics execution path before modifying behavior.

Trace:

```text
RigidBodySimulation
-> PhysicsWorld
-> forces/gravity
-> broadphase
-> pair filtering
-> narrowphase
-> GJK
-> EPA/contact generation
-> manifolds
-> constraints
-> solver
-> integration
-> ECS transform synchronization
```

Document:

* files;
* classes;
* functions;
* object ownership;
* data structures;
* hot loops;
* update frequency;
* mutation boundaries.

## Deliverable

Create:

```text
docs/physics/PHYSICS_REFACTOR.md
```

## Code modification

Production physics behavior should not change.

## Acceptance Gate

Architecture is understood and documented.

---

# 6. Phase 01 — Physics Performance Baseline

## Objective

Establish reliable performance measurements before optimization.

Measure every major physics subsystem.

Include special GJK metrics because profiling has already identified GJK as the largest current physics hotspot.

Track:

```text
Broadphase
Pair filtering
GJK
Support mapping
EPA/contact
Manifold
Solver
Integration
PhysicsWorld total
```

Create a repeatable physics benchmark.

## Acceptance Gate

Baseline numbers exist for multiple rigid-body counts.

Profiling overhead is controlled.

No physics behavior changed.

---

# 7. Phase 02 — Mathematical Correctness and Numerical Robustness

## Objective

Fix confirmed mathematical defects before performance refactors.

Investigate and test:

```text
zero-vector normalization
zero quaternion normalization
coincident centers
GJK degenerate directions
near-zero solver denominators
LCP pivots
C / dt penetration correction
inverseMass == 0
degenerate barycentric denominator
are_same_point Z epsilon bug
NaN/Inf propagation
```

Add Debug finite-state assertions.

## Important

Do not change quaternion coordinate convention in this phase.

## Acceptance Gate

Regression tests demonstrate the corrected behavior.

No unexpected trajectory or collision regressions.


---

# 8. Phase 03 — PhysicsBody Derived Data and Transform Refactor

## Objective

Remove repeated body-space/world-space calculations.

Investigate caching:

```text
rotation matrix
inverse rotation
world inverse inertia
world AABB
center-of-mass transform
other repeated derived values
```

Use dirty flags where appropriate.

Create golden rotation tests:

```text
local -> world -> local
world -> local -> world

90° X
90° Y
90° Z

rotated asymmetric box support
rotated inverse inertia
```

Only modify quaternion/transposition conventions if tests prove the current convention wrong.

## Acceptance Gate

Transform tests pass.

Cached values produce identical expected results.

Performance before/after is documented.

---

# 9. Phase 04 — Broadphase Refactor

## Objective

Improve broadphase scalability and memory behavior.

Investigate:

```text
SweepAndPrune1D
endpoint reconstruction
_alloca usage
sorting
qsort comparator
bodyCount^2 reserve
candidate rejection
```

Target improvements:

```text
persistent endpoint storage
capacity reuse
temporal coherence
incremental sorting
valid comparator
static/static filtering
collision layers/masks
safe sleeping filtering
```

Do not replace SAP with another major structure unless profiling proves it is still necessary.

## Acceptance Gate

Candidate pair correctness preserved.

Before/after scaling documented.


---

# 10. Phase 05 — GJK Core Optimization

## Objective

Optimize the currently measured primary physics hotspot.

Analyze:

```text
GJK call count
iterations
simplex operations
termination
degenerate cases
allocation behavior
```

Target:

```text
zero heap allocation in GJK hot loop
std::array<SupportPoint, 4> simplex
explicit simplex count
support A/B points retained
remove unnecessary normalization
squared-length tests
iteration limit
duplicate support detection
lack-of-progress detection
finite-value validation
scale-aware epsilon
```

Do not change collision results merely to reduce iteration count.

## Acceptance Gate

GJK regression tests pass.

Benchmark shows measured before/after GJK performance.

---

# 11. Phase 06 — Support Mapping Optimization

## Objective

Reduce the cost of the operation called most frequently by GJK.

Analyze current support functions.

If vertices are repeatedly transformed into world space:

```text
world direction
-> transform once into local space
-> search local support
-> transform only selected support point back to world
```

Use cached body rotation data.

Implement analytical support where appropriate:

```text
Sphere
Box
Capsule
```

Evaluate convex-hull support caching/hill climbing only if profiling justifies it.

## Acceptance Gate

Support mapping correctness tests pass.

GJK collision behavior remains correct.

Support call time improvement is measured.


---

# 12. Phase 07 — Specialized Narrowphase

## Objective

Avoid generic GJK where cheaper robust primitive algorithms provide measurable benefit.

Evaluate:

```text
Sphere-Sphere
Sphere-Box
Box-Sphere
Box-Box
```

Benchmark OBB SAT for Box-Box against GJK where appropriate.

Keep generic GJK as convex fallback.

## Acceptance Gate

Equivalent collision tests pass.

Performance benefit is demonstrated.

---

# 13. Phase 08 — Pure Collision Prediction

## Objective

Remove mutation from collision queries.

Replace patterns equivalent to:

```text
Integrate(+t)
collision query
Integrate(-t)
```

with immutable predicted transforms.

Collision detection should read live body state but not mutate it.

## Acceptance Gate

Prediction regression tests pass.

No state drift after collision queries.

Before/after CPU cost documented.

---

# 14. Phase 09 — Persistent Contact Manifolds

## Objective

Reduce repeated expensive narrowphase work for stable contacts.

Maintain bounded persistent contacts for recurring body pairs.

Track:

```text
manifold cache hits
contacts reused
contacts discarded
GJK calls avoided
```

Pay special attention to box stacks and resting contacts.

## Acceptance Gate

Stable contacts remain correct.

Stack behavior remains stable.

GJK call reduction is measured.

---

# 15. Phase 10 — Constraint Solver Refactor

## Objective

Improve solver performance and numerical behavior.

Profile:

```text
constraint construction
pre-solve
iterations
impulse calculation
friction
penetration correction
```

Replace tiny heap-backed Mat/Vec structures where dimensions are fixed.

Prefer:

```text
std::array
GLM fixed-size matrices
specialized value types
```

Evaluate solver warm starting using cached impulses.

Do not simply reduce iteration count to improve FPS.

## Acceptance Gate

Constraint regression tests pass.

Stack stability is preserved or improved.

Allocation and timing improvements are measured.

---

# 16. Phase 11 — Fixed Physics Timestep

## Objective

Separate physics simulation frequency from render frequency.

Replace render-dependent/clipped timing with:

```text
steady_clock
fixed timestep
accumulator
retained remainder
controlled backlog clamp
optional render interpolation
```

Test equivalent scenarios at different render rates.

## Acceptance Gate

30 / 60 / 120 / 144 / 240 FPS rendering produces approximately equivalent physics trajectories.


---

# 17. Phase 12 — Sleeping System

## Objective

Avoid repeatedly processing stable inactive bodies.

Implement or improve sleeping based on:

```text
linear velocity
angular velocity
stable time threshold
contact state
```

Wake on:

```text
impulse
force
transform modification
meaningful active collision
```

## Acceptance Gate

Bodies do not incorrectly remain asleep.

Stacked scenes remain stable.

Active-body reduction and physics timing improvement are measured.


---

# 18. Phase 13 — Physics Islands

## Objective

Build connected components of bodies and constraints.

Use islands for:

```text
sleep decisions
solver organization
future parallel execution
```

Do not parallelize yet.

## Acceptance Gate

Island membership tests pass.

Physics behavior remains equivalent.


---

# 19. Phase 14 — Physics Ownership and Memory

## Objective

Establish deterministic resource ownership.

Investigate:

```text
PhysicsWorld ownership
PhysicsBody ownership
Shape ownership
start/stop/restart
temporary physics containers
```

Prefer explicit RAII ownership.

Reuse:

```text
candidate pair storage
contact storage
manifold storage
solver scratch buffers
```

Avoid `bodyCount * bodyCount` reservation.

## Acceptance Gate

Start/stop/restart leak tests pass.

No new ownership ambiguity.


---

# 20. Phase 15 — Data Layout and Cache Behavior

## Objective

Evaluate cache efficiency only after algorithmic bottlenecks have been addressed.

Profile hot fields:

```text
position
orientation
linear velocity
angular velocity
inverse mass
inverse inertia
AABB
active/sleep state
```

Do not perform a full AoS-to-SoA rewrite without evidence.

Prefer targeted improvements.

## Acceptance Gate

Data-layout change has measured benefit.

No architecture complexity without measurable value.


---

# 21. Phase 16 — Parallel Narrowphase

## Objective

Parallelize independent collision queries only after they are pure.

Use existing TBB where appropriate.

Use:

```text
candidate pair partitioning
thread-local contact buffers
final merge
```

Do not use one mutex around every contact insertion.

Do not allow GJK to mutate PhysicsBody state.

## Acceptance Gate

1-thread and multi-thread results are correct.

No races under available analysis tools.

Scaling is measured.


---

# 22. Phase 17 — Parallel Island Solver

## Objective

Parallelize independent physics islands only if the solver remains a measured bottleneck.

Different islands may be processed independently.

Do not naively parallelize constraints belonging to the same island.

## Acceptance Gate

No races.

Simulation remains stable.

Scaling across worker counts is measured.


---

# 23. Phase Completion Requirements

A phase is NOT complete merely because the code compiles.

Every phase must satisfy:

```text
implementation complete
+
Debug build passes
+
Release build passes
+
relevant tests pass
+
benchmark executed
+
before/after documented
+
git diff reviewed
+
review document created
```

---

# 29. Final Principle

This project is not a race to finish all phases.

The objective is to maintain a chain of known-good physics-engine states:

```text
Baseline
   |
   v
Approved Phase 00
   |
   v
Approved Phase 01
   |
   v
Approved Phase 02
   |
   v
...
```

At every point:

```text
HEAD = latest human-approved physics implementation
```

No unreviewed phase is allowed to become the baseline for the next phase.
