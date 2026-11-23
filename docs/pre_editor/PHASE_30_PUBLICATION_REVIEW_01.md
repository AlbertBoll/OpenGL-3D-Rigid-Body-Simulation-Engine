# PRE_EDITOR Phase 30 Motors approval

The Owner approved Motors and the corrected Scene Viewport interactions, including hover visibility and Jolt as the default backend. The final ordinary Release x64 executable has SHA-256 `78b4f0abc73fa90c3360105bbe9de5a8bda9d08c7c14f6d2a13f8c531b7ab5df`. The Owner's personal executable hash was unreported. This approval binds the current source to the retained candidate evidence. [Owner decision](PHASE_30_FINAL_OWNER_APPROVAL_01.md) · [Candidate and reuse record](evidence/phase-30/acceptance-and-reuse.json)

| Gate | Final disposition |
|---|---|
| A_PHYSICS | PASS for the bounded powered-hinge semantics on Jolt and Native. [Qualification](evidence/phase-30/motor-qualification.json) |
| B_SAFETY | ACCEPTED_EXCEPTION for Candidate 03 Jolt Contact. Its numerical FAIL remains recorded for 41.050 mm peak penetration against 10 mm, recovery at tick 235 against deadline 221, and a 5.604 mJ braking residual rise against 1.250 mJ. [Jolt decision](evidence/phase-30/jolt-contact-acceptance-01.json) |
| C_COST | Two independent ACCEPTED_EXCEPTION results. Native Contact observer timing remains INCOMPLETE, with a 0.347140 ms upper bound against 0.333333 ms. Editor CPU non-regression remains NOT_DEMONSTRATED. The final build used 4.860 and 4.910 ms more main-thread CPU per frame in matched Idle windows. Mean frame time held near 6.06 ms. [Native decision](evidence/phase-30/native-contact-acceptance-01.json) · [Editor CPU decision](evidence/phase-30/editor-cpu-acceptance-01.json) · [Matched measurements](evidence/phase-30/release-focused-measurements.json) |
| D_EDITOR / E_INTERACTION | PASS for the documented ordinary Editor exposure, readbacks, supported interactions and accepted viewport changes. The frozen matrix has 962 covered rows and 49 component absences. Historical observations retain their tested identities. [Technical handoff](evidence/phase-30/editor-handoff.json) |
| F_OWNER | PASS for the observed Motors behavior and corrected Editor interactions. [Owner decision](PHASE_30_FINAL_OWNER_APPROVAL_01.md) |

The Owner retains the P1 Editor CPU risk. Source attribution, power impact, recurrence frequency and the recovery trigger are unknown. A reproducible source-specific trigger, worsening pacing, increased input latency or relevant topology evidence reopens the finding. The mixed-refresh HDMI observation remains separately unresolved. [Investigation](PHASE_30_FRAME_CPU_INVESTIGATION_01.md)

The sole predecessor for this publication is the Phase 29 Owner-accepted source archive at `ffa89a2708f34e8c0c194c1201728b565e2ba447`. Its production qualification and formal checkpoint remain incomplete. The publisher binds the milestone tag, archive tree and parent, Phase 28 approval, and exact Owner records. [Predecessor authority](evidence/phase-30-publication-predecessor-01.json)
