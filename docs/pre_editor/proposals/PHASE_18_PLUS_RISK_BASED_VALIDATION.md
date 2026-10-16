# Prospective PRE_EDITOR risk-based validation revision

**PROPOSED FOR OWNER-CONTROLLED WORKFLOW UPDATE; NOT ACTIVE.** Prepared at the owner's direction during Phase17 final closure. This proposal is not a Phase17 approval gate, does not alter an existing contract, and does not authorize START PRE_EDITOR PHASE18. Completed history and publication guards remain unchanged. Activate through the appropriate owner-controlled AGENTS/skill/governance update before Phase18 execution.

## Risk classes

Classify a finding by severity, likelihood, recoverability and the specific affected contract. Record the evidence and uncertainty. Frequency alone cannot downgrade catastrophic consequences.

| Class | Meaning | Disposition |
| --- | --- | --- |
| P0 — Critical | Crashes, memory corruption, ownership/lifetime violations, data loss, severe correctness or integrity failures. Rare catastrophic failures remain P0. | Mandatory block until demonstrated correction and affected validation. No cosmetic or environmental label may hide a P0 path. |
| P1 — Core functional | Normal user workflows, essential phase functionality and common integration behavior. | Focused automated regression plus appropriate owner acceptance. Unresolved core behavior blocks. Reuse unchanged successful evidence only with identity and impact proof. |
| P2 — Low risk | Rare, recoverable, noncritical edges; minor cosmetic differences; environment-specific behavior with bounded impact. | May be recorded and deferred with a reasoned impact/likelihood/recovery assessment, named residual owner and revisit trigger. Publication is not automatically blocked. An explicit mandatory contract cannot be silently downgraded; changing its acceptance policy remains an owner decision. |

Unknown severity is not automatically P2. Investigate only enough to bound the risk and decide disposition. A reproducible ordinary workflow defect remains P1 even if an individual run happens to pass. A test implementation failure has its own infrastructure classification and does not by itself establish production failure or production correctness.

## Bounded matrix before implementation

Freeze the phase matrix before editing behavior: exact source ownership, intended behavior, risk class, relevant dependencies, checks/configurations, human coverage, retained numeric thresholds, workloads, evidence reuse, count/time budgets and stop conditions. Define the completion claim for each check. Adding a new requirement after observing results requires an explicit dependency/contract reason or owner policy decision; do not grow an unbounded edge-case campaign.

Normal validation follows changed code and consumed dependencies. Choose the smallest checks that establish the affected P0/P1 contracts. Do not default to full-solution or every historical application suites. Compile affected shared interfaces where required; retain meaningful Debug/Release differences. Unchanged components may reuse successful evidence when source, generated inputs, toolchain, dependencies, flags, assets/settings and workload identities match. Observer-only changes may reuse production coverage when separately proven to preserve stimuli, assertions, measurement boundaries and result meaning. Label original and fresh evidence distinctly.

## Infrastructure preflight and correction

Before an expensive native process, verify fixture geometry/visibility and frame-publication premises, actual translation-unit selection, command/output paths, consumed generated dependencies, and observer storage versus workload/duration. Prefer bounded streaming summaries with complete assertion/failure accounting when frame counts depend on elapsed time. A fixed Debug sample count is not a Release capacity bound.

Automatically correct demonstrated mechanical compilation, fixture, harness, observer and evidence-plumbing defects within admitted scope and budgets. Preserve each failed attempt. Do not seek repeated owner approval for unambiguous mechanical repairs. Revalidate the affected evidence path and reuse unaffected matching coverage; never turn an unexplained FAIL into PASS, suppress an assertion or repeat an unchanged runtime for a favorable result.

The correction loop remains bounded by the frozen matrix, aggregate limits and a concrete causal proof. Unexplained production failures, exhausted budgets, conflicting correction policies or necessary contract changes produce one consolidated evidence-based handoff. No hidden budget resets or endlessly replenished qualification loops.

## Native environment and expensive suites

Use ordinary owner desktop preparation and lightweight eligibility checks first. Intrusive process isolation is justified only by demonstrated material interference and explicit ownership/restoration authority. Foreground identity, point visibility and process absence are separate observations. A failed identity/access query fails closed where that proof is mandatory; do not infer external causality from a generic access error or a later successful run.

Extended edge-case/stress suites run periodically or when their relevant code, dependencies or risk changes, using separately bounded schedules and preserved evidence. They are not automatically repeated at every phase. A scheduling policy cannot defer a known P0 defect or a required affected P1 regression.

## Owner decisions and publication

Reserve owner decisions for architecture, production semantics, source-scope expansion, genuine risk acceptance and mandatory acceptance-policy changes. Present one concrete proposal with evidence, alternatives only where material, cost and stop conditions. Routine implementation choices and mechanical repairs remain autonomous.

Keep the established07–55 fast workflow: bounded execution -> immutable READY/evidence closure -> owner acceptance -> explicit APPROVE -> internal guarded seal/finish/commit/tag/push and verified publication. No standalone SEAL command, extra confirmation by habit, broad approval-time campaign reconstruction or automatic next phase. Preserve canonical RBS raw/index bytes, protected work, source identity, destination/tag/ref and evidence-integrity guards.

The final record distinguishes PASS, FAIL, incomplete evidence, owner acceptance and accepted P2 deferral. It names original tested binaries and never invents the identity of an owner-inspected executable. Diagnostic success is not acceptance. Historical failures remain immutable even after a justified correction or risk disposition.

## Required activation record

Before applying this proposal to Phase18 or later, the owner-controlled update should record the exact AGENTS/skill/contract deltas, prospective effective phases, P0/P1/P2 classification authority, P2 deferral record format and periodic-suite triggers. No helper integrity/publication guard is implicitly changed. This document is preparation only; its adoption is independent of Phase17 qualification and approval.
