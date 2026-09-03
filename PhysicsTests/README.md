# Physics Regression Tests

`PhysicsTests` is the headless regression target for physics mathematical
correctness and numerical robustness. It does not initialize SDL, OpenGL,
ImGui, renderer state, or runtime assets.

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
