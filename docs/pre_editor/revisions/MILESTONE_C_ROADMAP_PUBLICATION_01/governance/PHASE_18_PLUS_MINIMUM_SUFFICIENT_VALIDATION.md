# Phase18+ Minimum Sufficient Validation and approval contract

Effective phases: 18-55, only after explicit owner acceptance and application of
review package PHASE_18_PLUS_MSV_REVIEW_01 and its activation record. This contract
is an unapplied proposal while supplied in that package's patch. Governance
acceptance does not authorize START, phase approval, commit or publication.

## Authority and unchanged boundaries

This prospective C0/V0/approval addendum reconciles the owner's activation directive
with `docs/pre_editor/proposals/PHASE_18_PLUS_RISK_BASED_VALIDATION.md`, which remains
unchanged provenance. Read C0/V0 and only the active `phases/PHASE_XX.md`; do not load
old campaigns by habit. Apply this addendum to validation selection, risk disposition
and human-before-publication ordering where older workflow or proposal wording
conflicts. Preserve phase functionality, exact ownership/file budgets, C0 technical
invariants, toolchain/dependencies, applicable numerical thresholds, explicit
mandatory gates and decisions due at the affected entry/gate. No blanket downgrade
of a mandatory check. A genuine scope/criterion conflict needs a specific owner
decision; a generic desire for cheaper validation is not a waiver.

Completed phases 00-17, their governance/receipts/commits/tags/publications and all
historical evidence remain immutable. Existing seals use unchanged finish-only;
never shrink, refresh or retrofit a seal. The checkpoint helper and its schema,
raw/index canonical RBS preservation, snapshots, source/input/evidence integrity,
protected work, index, destination/ref/tag and publication guards are unchanged.
The old contract heading 'Performance admission P0' is a performance protocol,
not this severity label; its affected Phase52/53 comparability gates remain binding.

## Small frozen acceptance matrix

Within an authorized START/REVISE, before behavior edits, record a small matrix in
the existing current review/compact READY preparation. This is autonomous planning
within approved scope, not a new owner approval stage or separate manifest system.
Each row names changed behavior/contract and risk rationale; affected targets,
configurations and dependencies; the smallest check or proven reuse; PASS criterion
and required human coverage. Record relevant stop/time/workload limits when a row
needs a bounded run or measurement. Keep existing accepted limits; do not invent a
fixed test count or universally mandatory performance campaign.

| Change or risk | Minimum sufficient evidence |
| --- | --- |
| Production code or shared interface | Debug/Release builds of affected targets and required dependency closure; matching unaffected products/evidence may be reused with identity and impact proof. Other-app compile/link only for an affected shared boundary or explicit gate. No default full-solution build. |
| Changed core functionality | Existing focused tests or straightforward deterministic checks of affected P0/P1 behavior, plus essential integration/regression smoke coverage. |
| Runtime integration, ownership or resources | Applicable RBS inspection and basic stability, resource lifetime and clean shutdown; inspect changed failure/cancellation paths where they bear on a material invariant. No blanket stress/corner-case matrix. |
| Material performance impact or explicit active-phase performance gate | Targeted measurement with predeclared workload, boundary, applicable limits and adequate samples for the actual claim. No unrelated percentile/profiler campaign. |
| Documentation/governance only | Focused static/contract checks and owner manual review; no engine build or launch. |
| Strictly mechanical production change | Diff/lexical proof plus affected compilation; runtime only if a concrete behavioral risk remains. |

RBS is the normal application runtime and manual inspection surface. Preserve V0's
other-app runtime limits and explicit applicable gates. Select only relevant M0
scenario fields; deterministic non-performance checks need no irrelevant hardware,
GPU timing, image-comparison or statistical manifest. Specialized observers, new
elaborate fixtures, intrusive desktop isolation or exhaustive cases require a
named material risk and an explanation why existing simple checks are insufficient.
Respect authority for any external process intervention; environment/access errors
are not proof that production is correct or that another process caused failure.

Freeze the chosen coverage before results. New demonstrated P0/P1 exposure must be
addressed, but expand only the affected rows with a causal dependency/contract
reason. Scope/acceptance-policy changes require the owner's decision; routine
in-scope mechanical repairs do not. Stop at sufficient evidence or a declared
limit, not after an arbitrary expanding campaign. No automatic periodic suite or
background schedule is introduced. A broader campaign needs demonstrated material
risk, an applicable gate or separate explicit owner direction and bounded scope;
periodic deferral never hides a known P0 or required P1 failure.

## Severity, authority and deferral

Classify by impact, likelihood, recoverability and uncertainty, not rarity alone.
Codex records the evidence-based classification; the owner decides genuine risk
acceptance and mandatory policy changes. Unknown severity is not automatically P2.

| Class | Required treatment |
| --- | --- |
| P0 Critical | Crash, memory corruption, ownership/lifetime or integrity failure: mandatory block until correction and affected validation demonstrate closure. Rarity never downgrades it. No P0 waiver under this policy. |
| P1 Core Functional | Essential user workflow or phase-functionality failure: focused validation required. Unresolved or insufficient required evidence blocks unless a specific owner disposition is already recorded under the rule below. |
| P2 Low Risk | Rare, recoverable, noncritical issue with bounded impact: may be documented and proposed for deferral with justified risk assessment; never silently downgrade a mandatory contract or conceal an unknown P0/P1 path. |

Use the existing review/READY findings list for a compact deferral record:
finding ID; actual FAIL/INCONCLUSIVE/not-run outcome and evidence identity; affected
behavior/criterion; class with impact/likelihood/recovery/uncertainty rationale;
mitigation or workaround; residual owner; concrete revisit trigger; and proposed
or accepted disposition with the owner acceptance reference. No separate database.
A revisit trigger is a relevant change, recurrence or milestone, not an invented
periodic service. Reconsider if impact or dependency assumptions change.

An eligible, explicitly disclosed P2 deferral may reach the READY human-review
handoff as proposed. The owner's final APPROVE accepts those disclosed P2 residuals
when its scope is unambiguous; record that acceptance before internal sealing.
No separate P2 approval command is necessary. Pending/undisclosed deferrals are not
accepted automatically. A failed explicitly mandatory criterion needs a specific
owner policy disposition; calling it P2 alone cannot waive it.

A P1 exception requires explicit owner acceptance naming the finding and affected
criterion, actual outcome/evidence, justification, mitigation, residual owner and
revisit trigger, including any bounded acceptance-policy amendment. Generic
APPROVE or governance activation alone does not grant that exception. It cannot
cover P0, unknown critical exposure or an integrity/publication guard. If absent,
stop with a concise blocked review; do not emit successful READY or seal.

Keep original FAIL, incomplete and diagnostic outcomes immutable. Accepted deferral
means a disclosed residual was accepted, not that the failed behavior passed.
In the final helper spec, use existing `ACCEPTED_OUT_OF_SCOPE` for a genuinely
owner-accepted deferred finding, with its evidence-backed disposition. If a failed
validation row requires an accepted exception, use existing `ACCEPTED_EXCEPTION`
with both `actual_outcome` and specific `owner_acceptance`, retaining check/command/
evidence. Use `NOT_APPLICABLE` only with a real reason, never for a failed check.
Unresolved required failures remain blockers. Do not alter helper checks/schema or
misrepresent broad core failure as out of scope just to make a spec eligible.

## Reuse and autonomous mechanical correction

Prefer existing matching build products. Reuse unaffected PASS only with relevant
source, generated-input, dependency/toolchain/configuration/flags and affected
assets/settings identities, discovery membership/modes, and an impact argument
that the claim remains valid. Keep original commands/dates/tested binary identities;
new or owner-rebuilt products are not retrospectively tested. Missing required
provenance blocks its reuse; disposable binary absence alone does not erase
historical PASS. If a current check needs that product, reconstruct it only during
a separately authorized phase/build action, not historical requalification.

Within authorized unsealed execution, autonomously correct demonstrated routine
compiler, fixture, runner, metadata and build defects using the existing bounded
correction loop. Stay within admitted ownership/file budgets, intended behavior,
APIs, architecture, resource contracts and accepted criteria/protocol/limits.
Preserve each actual failed/incomplete attempt and the minimal causal diagnosis;
rerun only affected checks and prove reuse for the rest. Observer-only changes
must preserve stimuli, assertions, measurement boundaries and result meaning.
No full qualification restart for a proved mechanical infrastructure error; no
unchanged retry to obtain a favorable result, hidden budget reset or relabeling.
Unexplained production failures, scope/policy changes or unresolved owner decisions
stop dependent work for one concise evidence-based review. This autonomy does not
extend into approval, sealed recovery or rejection.

Keep compact versioned evidence and exact original product identities. Do not
create duplicate Candidate binaries merely to preserve a correction label; use
matching products where evidence isolation and required current identities permit.
Never overwrite failure evidence, live products or required input dependencies.

## Mandatory human review and guarded approval

Every phase follows authorized implementation -> necessary automated checks ->
concise review -> STOP for owner manual inspection. The review states functionality,
changed files, PASS/FAIL/incomplete/reused results, deferred risks and practical
inspection criteria/configuration/path. Required handoff text:

**READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.**

RBS runtime inspection is required when relevant; static-only work still receives
owner manual review without an invented launch. Automated PASS, prior Phase17 owner
acceptance and approval-like praise never substitute for this phase's human review.
No forced full-solution rebuild, build-count report or standalone SEAL command.

Only explicit `APPROVE PRE_EDITOR PHASE XX` accepts the owner's final inspected
bytes/manual result and authorizes final approval. Preserve the existing fast path:
compare only READY's relevant inputs; a match reuses evidence without build/run.
Owner edits receive only required affected validation with unaffected reuse. This
does not require another routine human pass when the owner has already inspected
those final bytes and affected checks pass. New failure/undisclosed risk or unstable
inputs stops before seal for an updated review; no repair/source editing in APPROVE.
Do not invent a human-inspected executable identity that the owner did not report.

Finalize one compact acceptance/reuse record and existing review/spec. In the
existing `contract_files`/relevant input closure, retain the helper-required active
phase and `docs/pre_editor/VALIDATION_CONTRACTS.md`, adding this addendum, its
activation record and retention supplement. Retain the canonical RBS raw/index
member even unchanged. Use unchanged internal seal then immediately finish once:
commit -> annotated approved tag -> branch/tag push -> verified COMPLETE. No writes,
builds, runs, regenerated evidence or extra confirmation between seal and finish.
Failures/interrupted seals follow existing exact finish-only/recovery guards.
No self-approval, premature publication, force-push, tag replacement or next phase.

## Artifact retention

Apply `docs/pre_editor/active/PHASE_18_PLUS_ARTIFACT_RETENTION.json` after successful
explicit owner approval and verified publication only. Its A/B/C classification
is mandatory: tracked runtime prerequisites are never automatic deletion targets;
canonical working outputs and active/next-phase dependencies, acceptance artifacts
and immutable evidence stay; only obsolete, unused, unreferenced, reproducible,
safe untracked generated EXE/PDB/temp/Candidate/Diagnostic outputs may be removed.
Preflight Git tracking and runtime/build staging dependencies plus containment,
ownership, links/hardlinks and current use. Never clear either bin root wholesale.
Preserve uncertain items; cleanup failure is nonblocking housekeeping. No new
framework, background service, rebuild, launch or historical requalification.

## Reconciliation with the preserved proposal

- Adopt its risk classes, bounded matrix, identity-based reuse and truthful history.
  Clarify owner acceptance of P2 and explicit P1 exceptions as above; no P0 waiver.
- Apply infrastructure preflight only to concrete affected premises. It does not
  mandate elaborate geometry/visibility observers or invasive desktop isolation.
- Replace blanket campaign/manifest ceremony with the smallest relevant checks;
  retain explicit technical gates and accepted quantitative limits when applicable.
  Periodic/extended suites are not automatically scheduled or run at every phase.
- Make final manual review and the exact READY handoff mandatory for every phase;
  preserve the existing three skills and guarded internal seal/finish workflow.
- Incorporate restored-DLL retention protections; Git recoverability does not make
  tracked runtime prerequisites disposable. Phase17 and its original proposal stay
  unchanged; its old campaign costs are not the default for a new phase.
