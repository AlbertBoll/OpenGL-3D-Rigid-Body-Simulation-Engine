# Phase 31 Movers rider material follow-up

| Owner visual review | Disposition |
| --- | --- |
| Wood066 rider | **FAIL**. The Owner identified the approved Motors wooden box as the reference. [Previous handoff](PHASE_31_RIDER_WOOD_01.md) |
| Motors material on Platform Rider | **PENDING OWNER REINSPECTION**. Thumbnail replacement follows visual acceptance in the reserved ordinary Editor inspection. |

## Material and ownership

Motor Base and Motor Bar use the scene's `assets.boxMaterial` on `assets.boxMesh`. Platform Rider now binds that same scene-local box material directly. The existing loader writes the published `Sphere/wood_diffuse` albedo to that handle. This removes the Wood066 override and allows the rider to receive the update after its initial fallback. Moving Platform keeps its separate floor-material clone. The rider is the only Movers drawable using the box material, so editing its surface leaves the platform and room independent. Scene changes create fresh material resources. [Movers change](../../logs/pre_editor/phase-31/rider-wood-02/material-change.diff) and [wood publication](../../RigidBodySimulation/src/RbsResources.cpp)

The sRGB color image is `Images/Sphere/wood_diffuse.png` (2048 by 2048, SHA-256 `773043b7b845c4c9886cc82727fbdf39dd396f135dbc8890d6a9ea1273e09113`). The material also retains the Motors linear normal, metallic, roughness and AO textures from `PBR/base_white_tile`. Metallic and roughness factors are both 1, dielectric reflectance is 0.08 per channel, and the cube uses one UV tile per face. All five staged textures match the approved Phase 30 Git objects. The Motors builder, shared material builder and async publication code match the approved checkpoint `6f83ca3f8589bdbcbc9bc892c6fac22453eeb38c`.

## Build and focused inspection

Ordinary RBS **Release x64 PASS**. Executable SHA-256 `3c603d3916780ea089edce19e975d99b8d4b0ae78ca5d93d5333f838ad6fe7f4`. The only production edit is in `InitializeMoverScene`. Its Physics construction and registration match Candidate 01. The frozen workload and isolated qualification binaries retain their original hashes. This correction used zero RBS launches and zero Physics steps. Historical Native Contact remains **FAIL** and Native Heap remains **INCOMPLETE**. Phase 31 remains **NOT READY**. [Build result](../../logs/pre_editor/phase-31/rider-wood-02/ordinary-Release/result.json) and [historical qualification](../../logs/pre_editor/phase-31/candidate-01/status.json)

In the reserved ordinary Release inspection, open **Motors / Free** and then **Movers** in Ready. For each scene, wait for its own `Async wood texture published: 2048x2048` log entry. Compare the Motor Base with the rider's top and side faces at similar viewing angles. Check grain direction, tiling, highlights and the platform finish. After Owner acceptance, use a clean main-viewport capture from that session to replace `Images/Editor/Scenes/movers.png`. The existing card still shows the earlier rider. [Current thumbnail provenance](../../logs/pre_editor/phase-31/candidate-01/scene-thumbnail.json)
