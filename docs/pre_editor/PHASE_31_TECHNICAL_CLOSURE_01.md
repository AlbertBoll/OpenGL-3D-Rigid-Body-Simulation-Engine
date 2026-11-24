# Phase 31 Movers technical closure review 01

**Phase 31 remains NOT READY. RIDER WOOD VISUAL REVIEW: PASS.** The Owner accepted the corrected Motors-box wood on the Movers rider. The attestation identifies the corrected appearance. The inspection did not record the process hash or backend. The material, platform, simulation and prior qualifications were preserved. Final Owner acceptance is pending. [Owner record](../../logs/pre_editor/phase-31/closure-01/owner-wood.json)

| Gate | Current result |
| --- | --- |
| A_PHYSICS | **PASS** for both providers' prescribed motion, commands, replay and role isolation. Debug safety passed 12 attempts with 10 completions and two expected rejections. |
| B_SAFETY | **Jolt PASS. Native FAIL.** Both Native replays peaked at 31.189 mm on tick 8. Recovery was confirmed at ticks 69–70 against a tick-10 deadline. At tick 570 penetration was 1.361 mm. |
| C_COST | **Jolt PASS. Native INCOMPLETE.** Both providers passed inclusive CPU and observer criteria. Native whole-Update heap counters cannot isolate incremental mover allocations. |
| D_EDITOR / E_INTERACTION | **PENDING.** Of 485 frozen rows, 162 reuse verified shared-widget observations, 37 have source-backed not-applicable dispositions and 286 await actual Movers evidence. The scene card still uses the earlier rider image. |
| F_OWNER | **PENDING.** The final inspection reservation remains protected. |

The existing Jolt qualification covers 3,240 successful steps. Both replays had a 12,459,616-byte Arena peak, 18,474 positive reservations and matched releases. No RBS process or Physics step was used for this review. [Original qualification](../../logs/pre_editor/phase-31/candidate-01/status.json) · [Native analysis](../../logs/pre_editor/phase-31/closure-01/native-review.json) · [Component progress](../../logs/pre_editor/phase-31/closure-01/component-progress.json)

**Ordinary Release x64 build PASS.** The final incremental build compiled `RbsUi.cpp`, linked RBS and verified staged assets. Executable SHA-256 `48962ae2d0bce9776c7a3f17031655940c0fb2dfd1b2c58510509ced8c962ded`. The Movers Inspector exposes effective world and body identities, fixed step, solver profile, mass, bounds, COM and inertia. Mesh-fit, scale and rebind use the stopped transaction. Platform provider fields are read-only. Surface and visibility share one Inspector section. The new controls have build and source evidence. Their ordinary interaction remains pending. [Final build](../../logs/pre_editor/phase-31/closure-01/ordinary-Release-final/result.json) · [Source impact](../../logs/pre_editor/phase-31/closure-01/editor-source-impact.json)

**Decision boundary.** The Native failure arises in shared contact handling. The retained traces support no mover-local correction or heap attribution. Three metered fixtures and at least three ordinary starts exceed the original five-start limit. The single proposed amendment covers Native Movers production scope and the two prospective Editor roles. It preserves both Native findings and the original criteria. [Owner decision proposal](proposals/PHASE_31_NATIVE_MOVERS_SCOPE_AMENDMENT_01.md) · [Reconciled ledger](../../logs/pre_editor/phase-31/closure-01/resources.json)

After authorization, use one ordinary technical Editor session to record the real Movers card and corrected thumbnail, then exercise both providers in the same process. Collect the following evidence by shared transaction and bind each observed property to its row.

- Select platform and rider in turn. Compare effective handles, mass, bounds and inertia to the provider. Check the platform restrictions and the rider impulse response.
- In Edit/Ready, stage a path, scale, mesh fit and current-platform rebind. Exercise valid Apply, invalid Apply with retained draft/world, Revert and Restore Defaults. Verify the material and provider properties against the selected object.
- Exercise Start, Stop, Reverse and Pause/Resume. Record signed direction, completed ticks, platform position and velocity, rider pair count and slip validity. Toggle the trajectory, velocity, contact and error displays.
- Exercise gizmo staging, Reset, the dirty switch guard, scene and backend switching. Bind readbacks to fresh identities and check selection/retirement. Record the Experimental disclosure only after the scope decision is activated and implemented.

The final Owner role follows completion of A–E. The previously accepted wood appearance needs no repeat review. [Current gate record](../../logs/pre_editor/phase-31/closure-01/status.json)
