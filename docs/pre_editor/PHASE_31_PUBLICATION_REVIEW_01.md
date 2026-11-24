# Phase 31 Movers approval review

The Owner approved the current Movers candidate after final manual Editor inspection. The ordinary Release x64 executable matches the accepted handoff at SHA-256 `9d5d56a6657cdf0bd96faf8d6854d139fc4f099443e5c67a78676253ca1b1e53`. The Owner identified the candidate by handoff and supplied a final manual PASS. The process hash was not independently recorded during that inspection. [Owner record](revisions/PHASE_31_OWNER_UI_EVIDENCE_01/owner-acceptance.json)

| Gate | Final disposition |
| --- | --- |
| A_PHYSICS | PASS for the admitted Jolt and Native trajectory, commands and deterministic replay. |
| B_SAFETY | Satisfied for the approved scope. Jolt passed. Native Movers retains the historical contact FAIL for peak penetration and recovery. |
| C_COST | Satisfied for the approved scope. Jolt passed its CPU, 32 MiB Arena, heap and lifetime checks. Native incremental steady heap attribution remains INCOMPLETE. |
| D_EDITOR and E_INTERACTION | Satisfied under the Phase 31 Owner-approved UI evidence method. |
| F_OWNER | PASS on final manual inspection. |

Jolt is the qualified primary backend for `C31_SINELINE_RIDER_01`. Native remains selectable as Experimental for this workload. Both Native replays reached 31.189 mm of penetration. Their recorded failures and the missing heap attribution remain attached to the original candidate. [Native scope decision](proposals/PHASE_31_NATIVE_MOVERS_SCOPE_AMENDMENT_01.md)

## Editor evidence

All 518 frozen rows have a disposition. The 323 retained PASS and 37 NOT_APPLICABLE records match the previous verified matrix. The 158 remaining rows use explicit Owner UI acceptance, including 125 earlier PASS rows whose expectations changed and 33 additions. The group records identify which technical measurements, source checks and shared controls apply. No new automated UI observations were made for those rows. [Amendment and evidence method](revisions/PHASE_31_OWNER_UI_EVIDENCE_01/AMENDMENT.md) · [Row dispositions](revisions/PHASE_31_OWNER_UI_EVIDENCE_01/component-dispositions.json)

The accepted UI includes explicit simulation playback, return to Edit with the working pose, live read-only Transform inspection, runtime linear and angular impulses, mouse and keyboard numeric Gizmo entry, two-decimal presentation and the accepted selection outline. Prior inspections, qualification runs and build identities retain their original scope. This approval used zero builds, Editor launches and Physics steps. The checkpoint receipt and approval journal record the outcome of the guarded publication transaction.

The source archive includes the phase-owned work. The input record identifies the existing local frame-timing sources present in the tested build. Their worktree changes remain protected outside this commit.
