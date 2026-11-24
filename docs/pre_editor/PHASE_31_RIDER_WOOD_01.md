# Phase 31 Movers rider wood

**Owner appearance finding remains PENDING REINSPECTION.** The Owner requested a natural wood surface for the dynamic Platform Rider. The corrected ordinary Release Editor is built. The Owner deferred the overlay-free thumbnail capture to the reserved inspection.

The previous rider cloned the generic box material before its asynchronous wood request completed. The clone kept the fallback albedo and inherited the floor's normal, metallic, roughness and AO maps. The delayed update targeted the generic material handle. The rider now owns a Standard PBR material from scene construction. The existing platform still clones its original floor material. [Scene change](../../logs/pre_editor/phase-31/rider-wood-01/material-change.diff)

| Rider material | Verified value |
| --- | --- |
| Base color | Packaged `Images/Sphere/Wood066_2K-JPG_Color.png`, sampled as sRGB with mipmaps. Source and ordinary staged bytes match SHA-256 `aedebfbe763532efb9378ae9ca4e8f720900359f140f5db773e5e2b4e038df19`. The asset decodes as 2048 by 2048 RGB JPEG under its existing package name. |
| Surface | Metallic 0, roughness 0.82, neutral white base-color multiplier, default dielectric reflectance 0.04. Base color is the sole texture. |
| Mapping | The existing 2 m cube supplies full 0-to-1 UVs on each face, with one texture tile per face. |
| Work | Scene construction requests the texture from the existing cache. Its first load decodes and uploads the image. Movers skips the unused generic async box request. The same mesh and draw path are used. Cold texture loading and frame time were not measured. |

**Verification.** Ordinary RBS Release x64 compiled, linked and staged successfully. Executable SHA-256 `20cc9adb90f6abd6647ff9827a9a379f27af2072a55995e569823b52a72cdba6`. The source change is confined to `InitializeMoverScene`. Reversing the recorded delta in memory reproduced the exact Candidate 01 source. All other admitted source, workload and qualification-binary hashes still match. This correction used zero Editor or fixture starts and zero Physics steps. [Release build](../../logs/pre_editor/phase-31/rider-wood-01/ordinary-Release/result.json) and [Candidate 01 qualification](../../logs/pre_editor/phase-31/candidate-01/status.json)

**Reserved inspection.** In Movers, inspect the top and side faces of Platform Rider in the ordinary lit viewport for natural grain and a matte response. Confirm the platform retains its prior finish. Capture the clean viewport in that session and use it to replace `Images/Editor/Scenes/movers.png`. Until then, the scene card retains its previous image (SHA-256 `9c5504210013ea12e50d196ed42d6db876822690d168884d6d733b11194d9d37`). [Existing thumbnail provenance](../../logs/pre_editor/phase-31/candidate-01/scene-thumbnail.json)

Phase 31 remains **NOT READY**. Candidate 01 Native contact remains **FAIL** and Native incremental heap attribution remains **INCOMPLETE**. [Native assessment](../../logs/pre_editor/phase-31/candidate-01/runs/native/assessment.json)
