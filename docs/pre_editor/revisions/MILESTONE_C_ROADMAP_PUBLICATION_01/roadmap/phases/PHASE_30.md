# PRE_EDITOR Phase 30 - Motors

**ACCEPTED ROADMAP CONTRACT / PHASE NOT_STARTED.** Milestone C. No implementation or phase activation is authorized. New inserted phase; former same-number phase is remapped by identity, not renamed in history.

## Dependencies and entry

Approved predecessor: PRE_EDITOR Phase 29. Additional providing phases/baseline: 22, 24, 26, 28, 29. Phase21 denotes a frozen qualification obligation, not an approved provider. Every other required predecessor must actually be approved/published, with the named entry decisions resolved.

## Bounded technical scope

Bounded velocity drive on the admitted joint axis; explicit enable, target speed, torque/impulse cap, timestep conversion, limit priority and wake/cache rules.

Accepted production budget: 5-8 files; 1-3 focused supporting checks/docs plus mandatory compact records. These explicit prospective envelopes replace the smaller original joint budgets under the recorded roadmap acceptance. Beyond them needs a scope amendment. Phase32 conformance records are documentation; production corrections need separately admitted scope.

Non-goals: No servo pose control, active ragdoll, vehicle controller or unconstrained infinite-force drive.

## Required evidence and exit

Positive/negative/zero target, loaded stall/saturation, disable, limit conflict, impulse bound, injected work and contact stability/cost; owner inspects drive behavior.

C0/V0/M0 and the activated Phase18+ Minimum Sufficient Validation rules apply. Before implementation freeze the smallest risk-based matrix and exact file allowlist within the accepted budget. Build affected RBS/GEngine Debug and Release closure; use deterministic focused checks and essential RBS integration, lifetime and clean shutdown; measure materially affected or explicitly required performance only. Reuse unaffected PASS with source/dependency/impact proof. Accepted thresholds, durations, loads and measurement boundaries stay unchanged. Missing required bounds/evidence block; no universal numeric tolerances are invented here.

Phase21 is FROZEN_UNAPPROVED. The Phase22 exception and Phase25 approval do not resolve joint entry. OD-06/PHY-01 needs a separately explicit admissible joint-entry disposition or demonstrated closure; this amendment neither grants a waiver nor reopens Phase21. OD-08 and the new drive/mover/conformance decisions must be accepted before their affected START/gate. Preserve all failed evidence, especially Phase21 Candidate11; no coupled-contact revival. P0 blocks; P1 requires closure or a specifically permitted owner disposition, never implicit APPROVE waiver.

All seven use the existing RigidBodySimulation app, its ordinary SphereBoxStack default, one fixed-step Physics scheduler and the existing resource/body/Scene/Transform ownership. Use immutable validated startup selection; no scene recompilation and no runtime hot-switching. Source locators and detailed per-phase acceptance/architecture are in the accompanying seven-phase technical plan and RBS design; exact members are selected before START, not authorized by a locator.

## Owner review

Execution stops at **READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.** Owner manually inspects the final candidate. Only explicit APPROVE PRE_EDITOR PHASE XX for this phase authorizes internal seal then guarded finish, commit, annotated tag, push and verified COMPLETE. No standalone SEAL, automatic next phase, or publication authorization is supplied by this proposal.

## Supporting contract design

[Seven-phase technical plan](../SEVEN_PHASE_TECHNICAL_PLAN.md) and [audited RBS preset design](../RBS_SCENE_PRESET_DESIGN.md) are incorporated for this phase. [Primary reference](../BOOK_REFERENCE.md) supplies the bounded conformance mapping.
