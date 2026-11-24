# Phase 31 direct Transform editing and surface placement

**Phase 31 is NOT READY.** The ordinary Release Editor demonstrated direct placement on Jolt and Native. The last incremental build corrects a saved Hierarchy preference warning discovered in that session. Its on-screen result and the remaining component interactions still need verification. F_OWNER and thumbnail Owner visual quality remain PENDING. The Owner's Rider Wood PASS stands.

## Using the controls

* In Edit/Ready, select one Movers Platform or Rider. With the viewport active, choose T, R or S. Drag to preview. Release commits one valid change. Ctrl+click an Inspector axis to enter metres, degrees or scale, then press Enter or finish the edit. Escape cancels a preview. Ctrl+Z and Ctrl+Y undo and redo a completed gesture.
* In the viewport Snap menu, enable Surface Snap to place a Jolt collider against scene geometry. Optional controls align local +Y to the hit normal, add a surface offset and resolve penetration within the maximum correction distance. End drops the selected collider downward even with drag snapping off. Free numeric entry preserves coordinates when placement aids are off and the pose is valid. Native supports verified clear poses and rejects unproved clearance. Surface casting and automatic correction require Jolt.
* Ordinary Transform commits use the stopped edit boundary. The Movers controller keeps its prescribed path. The separate path center, collider and other structural changes still use their staged controls. Ragdoll, angular joint, multi-selection and camera constraints retain their component-specific editing paths.

## Ordinary Release evidence

| Check | Observed result |
| --- | --- |
| Jolt Rider gizmo and history | The live translation preview reached Y 7.54674 m, then committed at 7.88602 m. One undo restored 6.8 m and one redo restored 7.88602 m. Rotation and scale committed on release. Escape kept the preceding scale of 2.73056. |
| Jolt contact placement | End placed the Rider at Y 6.7502 m. With 0.25 m surface offset and normal alignment enabled, the tilted, scaled Rider settled into the authored pose at Y 7.36548 m and X 10.375 m. These were edit-time placements on a horizontal platform. |
| Platform preview separation | The Jolt provider reported (10, 5.5, 0) while X 10.625 m was being typed. Enter committed the snapped pose. Target and actual position then showed (10.62, 1, 0) at display precision. |
| Native clearance | A Platform edit committed at X 10.875 m. A Rider edit committed at Y 9.125 m. An overlapping Y 1.0002 m proposal displayed a diagnostic and restored 9.125 m. |
| Work during preview | Thirteen provider initializations correspond to startup, eleven successful edit/history actions and one backend switch. The cancelled drag and the rejected overlap triggered zero reconstructions. CPU frame time was not measured. |
| Execution | One supervised ordinary Release process, PID 20532, ran for 575.991 s with zero attempted or completed Physics steps and zero retries. Scheduled WM_CLOSE completed normally with exit code 0. |

The process loaded `22aed338b62edc84df67c490e8a6f3c6d91e131281aa9c175b4e60ee0db62175`. The screenshots, process counters and exact tested source and executable are retained. [Observed rows](../../logs/pre_editor/phase-31/direct-transform-01/observation-direct-placement.json) · [Resource ledger](../../logs/pre_editor/phase-31/direct-transform-01/resources.json)

## Saved Hierarchy preferences

The first successful direct transform produced a warning that saved editor overrides were skipped. A preset signature includes the pose. The old reconstruction carried saved signatures across edits for angular endpoints alone. The correction extends that existing identity migration to the two bound Movers roles after successful reconstruction of the same scene. It verifies the role key and previously saved signature before carrying the preference forward. Loss of a particular renamed or hidden body was not observed in the process. [Observed warning and correction](../../logs/pre_editor/phase-31/direct-transform-01/presentation-finding.json)

The incremental Release x64 build compiled the corrected application translation unit, linked successfully and verified the staged runtime. The final executable is `2af80616b55809db57859e2b1f26b6db16ac7aca1b13b065664cd23481d45767`. Its presentation change has source and build evidence. [Release build](../../logs/pre_editor/phase-31/direct-transform-01/ordinary-Release/attempt-03/result.json) · [Source comparison](../../logs/pre_editor/phase-31/direct-transform-01/source-impact-final.json)

## Remaining technical acceptance

The versioned inventory contains 493 entries. **412 PASS, 37 NOT_APPLICABLE and 44 PENDING.** Eleven direct-edit rows gained actual evidence. Eight existing name/visibility rows were reopened for the last correction. The previous 485-row matrix is retained for the earlier Apply-based behavior. [Exact pending rows](../../logs/pre_editor/phase-31/direct-transform-01/component-progress.json) · [Evidence verification](../../logs/pre_editor/phase-31/direct-transform-01/component-evidence-check.json)

| Remaining scope | Minimum focused check |
| --- | --- |
| 21 ordinary room, camera and light Transform rows | Use a representative room collider, camera and each applicable light kind. Verify allowed fields and the generic completion route. Extend evidence only across identical controls, capabilities and transaction paths. |
| 8 Movers name and visibility rows | Rename or change visibility on bound roles. Verify the preferences across a direct edit and history replay. Include selection after reconstruction and a cross-backend check. |
| 9 Movers pose and gizmo rows | Exercise the remaining role and provider combinations for Platform and Rider rotation, scale and gizmo completion. Include precise typed degrees and scale. |
| 2 controller center rows | Exercise the separate structural path-center field and verify the authored and effective Platform position at a nonzero initial phase. |
| 4 placement and history rows | Exercise independent Jolt penetration correction with drag surface snapping off. Check Native End and surface unavailability in the viewport, then Native undo/redo after an invalid edit. |

No technical session remains released. One proposed follow-up on the corrected executable would stay in Edit/Ready, use at most 600 seconds and zero attempted steps, with zero retries. It requires separate technical authority. The final Owner session remains reserved until A–E are ready. The ordinary, unsealed development executable is available at `bin/Release/RigidBodySimulation/RigidBodySimulation.exe`.

Physics qualification retains its original candidate identities. Jolt motion and cost evidence are reused. Under the approved Native Movers-only scope, historical contact penetration/recovery remains FAIL and incremental heap attribution remains INCOMPLETE. D_EDITOR and E_INTERACTION remain open on the direct editing revision. [Prior gate evidence](../../logs/pre_editor/phase-31/technical-editor-04/technical-gates.json) · [Interaction contract](active/PHASE_31_DIRECT_TRANSFORM_AND_PLACEMENT_01.md)
