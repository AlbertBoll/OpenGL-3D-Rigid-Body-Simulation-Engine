# Physics Regression Tests

`PhysicsTests` is the headless regression target for physics mathematical
correctness and numerical robustness. It does not initialize SDL, OpenGL,
ImGui, renderer state, or runtime assets.

## Contact telemetry contract

`PhysicsProfileSnapshot` is optional instrumentation. Without
`GE_ENABLE_PHYSICS_PROFILING`, every snapshot is zero and profiling macros are
no-ops. Correctness assertions inspect live manifolds, constraint bindings and
body state in both modes. They must not require nonzero disabled counters.

With profiling enabled, `generatedContactCount` accumulates finite generated
contact candidates before manifold insertion/deduplication (including expanded
box-face contacts and transient positive-TOI contacts). `manifoldContactCount`
is the retained manifold-contact total sampled before PreSolve;
`solverConstraintCount` samples those contacts eligible for active resting
response at that point. It excludes transient TOI contacts, scalar friction
rows and iteration multiplicity. PreSolve may retire invalid contacts, so it
is not a count of contacts that actually applied an impulse. ADD counters
accumulate until `ResetPhysicsProfile`; SET counters describe the latest sample.

`PhysicsTests.exe --contact-telemetry` checks the four-contact awake box-face
fixture, constraint bindings, unchanged unforced motion and reset semantics.
The same fixture requires exact nonzero instrumentation when profiling is
enabled and explicit zero snapshots when disabled. `--box-manifolds` and the
default suite also run these checks.

Generate and run on Windows:

```powershell
& .\vendor\bin\premake\premake5.exe vs2022
msbuild .\PhysicsTests\PhysicsTests.vcxproj /m /nologo `
  /p:Configuration=Debug /p:Platform=x64
.\bin\Debug\PhysicsTests\PhysicsTests.exe
```

The executable returns nonzero if any case fails and covers:

- zero custom-vector and quaternion normalization;
- finite GLM vector/quaternion fallback normalization;
- zero-normal orthogonal basis construction;
- degenerate terrain and GJK barycentric denominators;
- duplicate-point epsilon behavior on all three axes;
- zero and near-zero LCP pivots;
- coincident static and swept sphere contacts;
- degenerate GJK search directions;
- box construction invariants plus exact face contact, slight penetration,
  separation, rotated contact, and a small exact-contact box stack;
- zero inverse mass under gravity;
- zero and near-zero penetration-constraint timesteps;
- finite body integration from a zero quaternion;
- local/world transform round trips and golden positive 90-degree X/Y/Z rotations;
- rotated asymmetric-box support, cached AABB, and inverse inertia;
- derived-data invalidation after direct pose, inverse-mass, and shape changes;
- warm-cache direct-orientation invalidation across rotation, AABB, inertia, and center of mass;
- counted proof that unchanged queries reuse shape bounds/inertia and revisions refresh them once;
- revision-safe public sphere-radius mutation;
- a one-step gravity trajectory and ordinary sphere overlap/separation results.
- broadphase equal-endpoint ordering and swept fast-body candidate retention;
- three-axis AABB, static/static, and reciprocal layer/mask filtering;
- persistent endpoint/scratch capacity reuse, incremental sorting, and safe
  rebuild after body membership changes.
- exact initial and incrementally updated candidate sets against a filtered
  swept-AABB brute-force oracle.
