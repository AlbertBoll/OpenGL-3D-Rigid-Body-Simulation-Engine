# Phase 30 Motors entry from the Phase 29 source archive

**Version 01 · Owner-authorized · applies once, to PRE_EDITOR 29 → 30.** The Owner’s directive “PRE_EDITOR PHASE 29 EXCEPTION-BASED MILESTONE TRANSITION” accepts the exact Passive Ragdoll archive as the predecessor for Motors entry. The repository used this form of prospective, entry-only amendment for Phase 22. Its separate publication exception applied exclusively to that earlier transition. [Entry precedent](PHASE_22_ENTRY_EXCEPTION.md)

## Predecessor and authority

For Phase 30 entry, the Phase 29 predecessor requirement in C0, the Motors contract and the execution skill is satisfied by the following published source and Owner decision. This substitution covers both references to Phase 29 in the Motors dependency clause. Other named providers still require their own completed publications.

| Required binding | Identity |
| --- | --- |
| Repository and branch | `AlbertBoll/OpenGL-3D-Rigid-Body-Simulation-Engine` · `origin/pre-editor/refactor` |
| Sole admissible source commit | `ffa89a2708f34e8c0c194c1201728b565e2ba447` |
| Git tree | `a145e744564c1ce7600ef2f902c94af05cf5a11a` |
| Sole parent · formally approved Phase 28 | `a983eac5e9f2c7993a17d90ef054ce216315c5d6` · annotated tag object `cc0d456d1eec388e03a4f0c65de15dfdc478d21b` |
| Archived Owner decision · SHA-256 of committed bytes | `33b37ede65ec61a9ccf3386e54f34c37065081ed2158a0d5f245e1a31424e66e` |
| Archived evidence index · SHA-256 of committed bytes | `6a7316aa6c543e0c191835f5c9f870cf55332f6b01e56805054ec46c2af2ef74` |

At entry, require the exact branch, HEAD, tree, parent, Owner records and completed Phase 28 checkpoint. A different commit, including a descendant, falls outside this exception. The versioned activation records these checks, the other provider identities and the disposition of the Motor entry checks. The commit and Owner records identify the handoff without an additional Git tag. [Owner approval](PHASE_29_OWNER_APPROVAL_WITH_DOCUMENTED_EXCEPTIONS_01.md) · [Archived evidence index](evidence/phase-29-owner-approval-exceptions-01.json) · [Entry activation](active/PHASE_30_ENTRY_EXCEPTION_ACTIVATION_01.json)

## Phase 29 disposition carried into Motors

| Item | Disposition |
| --- | --- |
| Milestone progression | **OWNER_ACCEPTED_FOR_PROGRESSION** |
| Production qualification and formal Phase 29 checkpoint | **INCOMPLETE**. Phase 28 remains the last formally approved checkpoint. |
| Owner visual review | **PASS** for the inspected Ragdoll’s overall behavior. The executable and backend were unrecorded. |
| Step Failure Propagation | **PASS**, six cases, eight attempted steps, three completed, expected controlled exit 1. |
| Jolt DROP / TILT | **Historical FAIL / NOT EXECUTED**. All four DROP violations and frozen limits retain their original identities. |
| Release cost and lifetime / Editor and interaction | **INCOMPLETE / INCOMPLETE**. The 976 property-context rows retain their recorded status. |
| Native Passive Ragdoll | **EXPERIMENTAL** for this feature. Both Native Release motion failures remain in the archive. |
| Ordinary Editor frame pacing | **INTERMITTENT_FRAME_PACING_ANOMALY / ROOT_CAUSE_UNKNOWN / DEFERRED** |

The Owner retains the recorded risks. Phase 29’s 6,496 administrative ticks stay on its ledger, with historical actual usage unknown. Phase 30 receives zero launches or ticks through this exception. The Phase 27–32 gate table, the component contract, the 32 MiB Arena policy and the Phase 32 conformance obligations continue to apply in their existing scopes. A later claim using a Phase 29 result must name the actual matching evidence and outcome.

## Phase 30 entry disposition

The Owner’s START instruction opens Motors entry preparation at the bound archive. The independent **C-MOTOR** admission remains open. The roadmap requires evidence-backed, workload-specific numerical limits before affected implementation or measurement. Earlier joint and Passive Ragdoll admissions apply only to their own workloads. [Motors contract](active/roadmap/MILESTONE_C_01/phases/PHASE_30.md) · [C-MOTOR decision register](active/roadmap/MILESTONE_C_01/VALIDATION_CONTRACTS.md) · [Chapter 22 authoring schema](active/PHYSICS_PHASE_28_32_BOOK_REFERENCE_AND_PARAMETER_AUTHORING_PLAN.md)

The source baseline already selects a stock Jolt hinge for a free hinge and a `BookHingeConstraint` containing a Jolt hinge for a limited joint. The pinned hinge exposes velocity target, torque cap and applied motor impulse. The Motor case must cover the wrapped limit path as well as free rotation, with effective readback through GEngine’s joint identity. The Native Experimental disposition covers Passive Ragdoll only. [GEngine hinge selection](../../GEngine/src/Physics/JoltBackend.cpp) · [Pinned Jolt hinge interface](../../external/jolt/Jolt/Physics/Constraints/HingeConstraint.h)

| Established design | Still required for implementation entry |
| --- | --- |
| One hinge velocity drive, signed rad/s, finite torque, per-step impulse cap `maxTorque * fixedStep`, hard-limit priority. Enabled zero speed is a brake. | Admit the exact motor/load/contact case under PHY-01/OD-06 and extended OD-08, including torque, timing, tracking/stall, limit error, injected work and recovery bounds. The book target of 2 rad/s and proposed 100 N·m cap identify a reference and a proposal, respectively. |
| Focused off, forward, reverse, zero, loaded stall, active stop and disable observations. Required Native behavior and Jolt hinge support are in scope. | Fix the OD-07 CPU/allocation comparison and observation window for that workload. Freeze the source allowlist within 5–8 production files and 1–3 supporting items, plus compact records. Complete the source-backed property inventory and the component verifier’s entry check before changing production. |

The standard publisher still expects a completed Phase 29 journal, approved tag and matching formal state before sealing Phase 30. Eventual Phase 30 publication from this archive requires a separately authorized, exact-predecessor publisher amendment and Phase 30’s own successful qualification and Owner approval. This entry decision issues no seal, journal, receipt or numbered approval tag.
