# Phase 31 Movers · final Owner inspection

The cold-start Release session displayed the final Movers thumbnail on Jolt and Native. Both Content Browser cards showed the wood Rider, its shadow and the full raised platform. The frozen component matrix now contains **448 PASS and 37 NOT_APPLICABLE**. Only the two card rows received new observations. Thumbnail visual quality and final Phase 31 acceptance remain with the Owner. [Gate status](../../logs/pre_editor/phase-31/technical-editor-04/status.json) · [Component matrix](../../logs/pre_editor/phase-31/technical-editor-04/component-matrix.json)

| Actual 160 × 90 card measurement | Jolt | Native |
| --- | ---: | ---: |
| Correlation with final 2048 × 1152 capture | 0.99334 | 0.99334 |
| Correlation with previous 1024 × 576 composition | 0.55799 | 0.55799 |
| Mean absolute error against source display RGB, out of 255 | 4.59 | 4.59 |
| Mean absolute error predicted by a second sRGB decode | 55.13 | 55.13 |

The entire displayed card raster was byte-identical across the backend switch. Both screenshots show the effective backend and tick zero. The source and staged PNG matched before the process started, with SHA-256 `34fd1e73f2a4c38c5cd1b4b7e2e0a6239976f37c0a2c2679c3fb3db582fc1c5d`. [Jolt screenshot](../../logs/ui/phase31-movers-technical-04/02-jolt-card.png) · [Native screenshot](../../logs/ui/phase31-movers-technical-04/03-native-card.png) · [Pixel comparison](../../logs/pre_editor/phase-31/technical-editor-04/card-pixels.json) · [Source PNG](../../GEngine/include/GEngine/Assets/Images/Editor/Scenes/movers.png)

The Owner authorized the existing timestamp-only relink for the cold-start session. Its SHA-256 is `e8a3cad4db415d3c541e2195f01b9a9a04f989d23bce10044663d271befe66a2`. A read-only full-file comparison located both differences in linker timestamps. Technical Editor 03 keeps its original binary identity. The one authorized process exited normally with code **0** after **90.510 seconds**, with **0 attempted and 0 completed Physics steps** and **0 retries**. [Executable comparison](../../logs/pre_editor/phase-31/technical-editor-04/identity-preflight.json) · [Execution ledger](../../logs/pre_editor/phase-31/technical-editor-04/resources.json)

Jolt qualified for `C31_SINELINE_RIDER_01`. Native remains Experimental for Movers. Its recorded rider contact penetration and recovery **FAIL**, and incremental steady heap attribution remains **INCOMPLETE**. The approved scope defers those two production qualifications. The accepted Rider Wood appearance and the earlier Gizmo, Apply/Revert and provider readback results retain their original evidence. [Technical gates](../../logs/pre_editor/phase-31/technical-editor-04/technical-gates.json) · [Native scope decision](active/PHASE_31_NATIVE_MOVERS_SCOPE_AMENDMENT_01.json)

## Owner session

Use the reserved ordinary Release inspection with `--scene movers --backend jolt`. The executable is [RigidBodySimulation.exe](../../bin/Release/RigidBodySimulation/RigidBodySimulation.exe), the working-directory root is `runtime/RigidBodySimulation/Release`, and the staged assets are in `bin/Release/assets`. The reservation allows one supervised Owner process, up to 1,800 seconds and 720 attempted Physics steps, with zero retries.

1. In the Content Browser, filter for **Movers** and judge the final card at its normal size. Check the grain, silhouettes, framing and color against the live scene.
2. In the Viewport, inspect the moving platform and freely simulated Rider. Start, stop, reverse and pause the motion within the reserved step limit. The accepted wood material and the completed technical transactions need no repeat qualification.

The final Owner session remains unused. `F_OWNER` and thumbnail visual quality are **PENDING**.
