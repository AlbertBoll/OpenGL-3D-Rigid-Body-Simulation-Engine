---
name: pre-editor-phase-approval
description: Approve one explicit PRE_EDITOR phase. For unsealed READY phases 07-58, compare only relevant inputs, reuse matching evidence or validate affected changes, then execute the guarded publication transaction. Existing seals/completed phases use finish-only. Excludes START, REVISE, REJECT, Rendering, Physics and ambiguous aliases.
---

# PRE_EDITOR phase approval

## Accepted prospective roadmap lookup

For an unsealed PRE_EDITOR phase 26-58, require the accepted
`docs/pre_editor/active/MILESTONE_C_ROADMAP_ACTIVATION_01.json` and load the
phase and common validation contracts only from
`docs/pre_editor/active/roadmap/MILESTONE_C_01/`.
Bind that activation record, its `VALIDATION_CONTRACTS.md` and the selected
`phases/PHASE_XX.md` in `contract_files`. Missing activation/successor blocks;
do not fall back to the old same-number contract or infer a number alias.
Original 30-55 meanings map to 33-58; new 30/31/32 are Motors/Movers/Book
Conformance. Original 00-25 contract paths, every existing seal and all completed
history retain their exact original bindings and finish-only rules.
The new activation extends the unchanged original MSV/retention rules through 58;
keep the original acceptance records/hashes, and map old performance owners52/53
to55/56 only in prospective obligations. Governance acceptance is not START or
APPROVE and supplies no missing technical admission. Rendering is unchanged.

Only explicit `APPROVE PRE_EDITOR PHASE XX` activates the named phase's standing
publication grant. XX is exactly 00-58. Quoted commands, praise, governance changes
and other modes are not approval. Later owner restrictions/revocation control.

## Choose the path

Completed Phases 00-06 are immutable history; never reopen or rewrite them.
Read current state and the named receipt/journal if present. An existing seal,
interrupted transaction or completed approval uses finish-only below. Never
revalidate, refresh fingerprints, narrow the sealed snapshot or reseal to bypass
a mismatch. Inconsistent sealed records block. The fast path below is only for
an unsealed expected READY candidate in 07-58 with no prior approval transaction.

## Phases 07-58: narrow comparison, then publication

APPROVE declares current canonical RBS bytes final and accepts the owner's manual
RBS result. Normal flow stays START -> owner review/edit/rebuild/run -> APPROVE.
No additional command, SEAL command, confirmation or post-validation human pass.
No full-solution Rebuild PASS, mandatory rebuild or project-count report. Keep
known failures accurate; PhysicsTests/unrelated apps are gates only where the
active phase contract explicitly requires them.

Use the prepared compact READY record/current spec and applicable owner/AGENTS
instructions. Do not load old reviews, campaign trees or broad historical
preservation inventories by habit. Resolve the few new record writers inside
existing PRE_EDITOR output paths; no Rendering destinations or reparse escape.

Perform only these scoped checks before choosing the path:

1. Confirm expected phase/candidate READY status, baseline/predecessor and matching
   compact READY identity/PASS coverage. Read the current receipt/journal only to
   select unsealed versus finish-only. The completed predecessor Git tag/current
   state establish ordering; do not audit all prior journals/campaigns.
2. Compare current canonical RBS and the handoff's explicitly relevant production,
   validation and behavior-input closure by content, membership and mode. Include
   the recorded exact discovery scopes and external dependency/tool identities.
   No repository/source-root recursive sweep or rehash of unrelated protected work.
   Do not trust timestamps alone; cost is bounded by actual relevant inputs,
   not promised constant time regardless of their size. Missing/incomplete READY
   coverage blocks; do not reconstruct a broad campaign to conceal the gap.
3. Check current branch/HEAD, index/unmerged state and Git changed-path names for
   exact ownership or predeclared protected work, then the configured destination,
   current branch ref and named tag for immediate conflicts. Read-only discovery
   never grants consent. Use one narrow remote-ref query if needed; leave full
   destination/policy/publication enforcement to the unchanged helper. No separate
   routine helper check/publication-target or duplicated preservation audit.

Classify by actual consumed role. Owner-rebuild executables/libraries/intermediates,
timestamps and generated logs are outputs: no validation rerun or approval block
solely for those changes, and no routine artifact hashing. Relevant build flags,
consumed generated headers, settings/assets/shaders and third-party/runtime
dependencies remain behavior inputs. Consult the declared roles when necessary;
do not promote incidental files from an old broad inventory into technical gates.

**Inputs match:** reuse the READY PASS coverage and original evidence identities.
Do not build Debug/Release, rerun tests, launch RBS, rehash archived raw evidence,
reconstruct old campaigns or regenerate broad preservation manifests. No full-
solution or repository-wide validation. Keep one compact approval record naming
READY identity, accepted RBS hash, input comparison/reuse decision and any actual
reported human findings. Retain original commands/dates/tested binary identities
by reference. Human binary identity is unknown unless reported; a source-evidence
reuse decision does not claim that owner-rebuilt outputs were automatically tested.

**Relevant inputs changed:** stop the fast path, record the actual dependency
impact and run only active-contract-required affected checks/configurations.
Reuse unaffected matching checks. Do not default to the entire Debug+Release
campaign; C0/V0 affected-code/shared-contract compilation requirements still
apply when triggered. Archive only new affected evidence, retain prior attempts,
and prepare a new compact validated identity for final current bytes. A failed
required check, unstable input or unknown relevant delta stops before publication;
no automatic repair, rollback, source editing, formatting or scope expansion.

### Compact transaction handoff

Finalize only the current compact approval record/review/spec. Preserve the
helper's existing phase/baseline/review/checkpoint_files, local_governance,
protected_files/contract_files/validation_input_hashes schema and canonical raw/
index RBS bytes/mode. Fingerprint the declared relevant inputs once
and reuse those results in the final spec. Fingerprint only newly written compact
records afterward; do not repeat the source/input pass. The helper's own snapshot
rechecks remain mandatory; do not duplicate them manually.
If a required affected run occurred, refresh only its affected identities.

Bind the compact READY/evidence identity, active contracts and separate owner
acceptance, not the recursive evidence archive. Seal only the prepared relevant
snapshot: exact owned source, required behavior inputs, current compact records,
and explicitly protected work/helper/policy boundaries. Keep derived outputs out
by default; any contract-required current artifact identity is separate from old
tested products, and a rebuild difference alone is not validation failure.
Never drop an input needed by the phase or shrink an already existing seal.

Reuse entries retain original PASS attribution, commands/dates and evidence
identities; new checks are labelled separately. Finish the compact manifest before
binding it in the spec. Bind the spec through local_governance, not a circular
self-hash. Preserve previous records; only current candidate pointers may change
before finish. Set READY TO SEAL AND FINISH; OWNER APPROVAL RECORDED.

Immediately execute the existing internal metadata seal and then guarded finish:
commit -> annotated approved tag -> push branch/tag -> verified COMPLETE. This
seal is an internal integrity boundary, not another owner command, review pause
or validation/audit campaign. Normal Git/index/snapshot/raw-blob/protected-work,
destination/ref/tag and publication guards remain unchanged.

Invoke once from `C:/dev/GEngine-pre-editor`:

```powershell
python -B .codex/tools/rendering_checkpoint.py --workflow pre-editor seal --spec <current-spec-path>
```

Only after successful SEALED output, immediately invoke the finish command below
once as the next operation. Skip its repeat record-reading step for this newly
created seal. No second approval pause, edits, builds, tests, application runs,
evidence writes/regeneration, fingerprint refresh, resealing or separate
check/publication-target calls between seal and finish. Never finish if validation
or seal fails. On seal/finish failure or interruption, preserve all records and
stop without automatic retry or repair. A later explicit APPROVE/resume with an
existing seal follows finish-only for that exact checkpoint and any existing
transaction. Never revalidate a sealed candidate to bypass a mismatch.

## Finish only: existing seal or completed phase (including 00-06)

Read only applicable owner/AGENTS instructions, this skill,
`docs/pre_editor/active/REFACTOR_STATE.md` and the named
`pre-editor-checkpoints/phase-XX.json`. Reuse instructions already loaded.
Confirm the named phase and receipt eligibility, no unresolved gates, and the
receipt/state branch `pre-editor/refactor` and remote `origin`. Do not follow
links into reviews, phase contracts, policy/grant records or evidence. The helper
loads and checks its own standing policy and sealed inputs internally.

Then invoke this command exactly once with shell working directory
`C:/dev/GEngine-pre-editor`, substituting the requested two-digit phase.
The helper verifies repository identity internally:

```powershell
python -B .codex/tools/rendering_checkpoint.py --workflow pre-editor finish --phase XX
```

The applicable standing grant needs no duplicate destination consent and no
`--authorize-*` arguments. Finish retains all existing seal, index, protected-work,
canonical raw/index blob, destination, exact staging, commit, annotated-tag,
publication and remote-verification checks. Keep those helper checks unchanged.

Do not run separate routine fingerprint, check or publication-target commands.
No repeated audit, builds, tests, application runs, benchmarks, evidence
regeneration, implementation changes or resealing on this finish-only path.
Do not inspect the helper implementation or other skills on the normal path.

On COMPLETE or ALREADY_COMPLETE, report phase, commit/tag and publication status
briefly, then stop. On any failure, report the helper's failure briefly and stop;
no automatic retry, repair, rollback or additional helper call. A later explicitly
requested resume uses this same single finish invocation and existing transaction,
never a replacement checkpoint. No force-push, tag replacement, hook bypass or
automatic next phase.

## Phases 18-58: final human acceptance, risk disposition and retention

For unsealed phases 18-58 after governance activation, use
`docs/pre_editor/active/PHASE_18_PLUS_MINIMUM_SUFFICIENT_VALIDATION.md` as the
prospective validation/approval addendum with the current compact READY record.
The owner must have received the concise functionality/changed-files/results/risk
review and manually inspected the final candidate, with RBS runtime when applicable.
Only explicit `APPROVE PRE_EDITOR PHASE XX` accepts final bytes/manual results and
authorizes publication. Do not infer it from governance acceptance or passing tests.
Do not ask for another routine confirmation or mandatory rebuild after that command.

No P0 failure may be waived. P1 failures block unless a specific, explicit owner
disposition identifies the finding, applicable criterion, evidence and bounded
residual risk; it cannot waive a P0 path or an integrity guard. Ordinary APPROVE
alone is not a P1 exception. Present eligible P2 deferrals clearly before approval;
final APPROVE may accept those disclosed deferrals under the addendum without a
separate ceremony. Preserve original FAIL/INCONCLUSIVE results and accepted residuals.
Use existing helper-supported finding/validation dispositions only as specified by
the addendum; never omit a failed check or invent PASS to satisfy the helper.

Keep the existing narrow relevant-input comparison: match reuses READY evidence;
a relevant delta receives only affected checks with unaffected proof reused. No
routine build, runtime launch or campaign reconstruction on a match. New failures,
unknown impacts or changed undisclosed risk stop before seal for an updated review;
no source repair or scope expansion in APPROVE. Bind the active addendum, activation
record and retention supplement in the existing contract/input closure, in addition
to the helper-required active phase and original VALIDATION_CONTRACTS.md. Preserve
canonical RBS raw/index bytes and every snapshot, evidence, protected-work, index,
destination/ref/tag/publication guard. Use the existing internal seal, immediately
followed by finish once; no intervening writes or extra approval/SEAL stage.

Only after successful owner-approved verified COMPLETE, apply
`docs/pre_editor/active/PHASE_18_PLUS_ARTIFACT_RETENTION.json` to declared obsolete
generated outputs. Preflight Git tracking and active/next-phase runtime/build
staging dependencies, including inputs under bin. NEVER automatically delete tracked
runtime prerequisites; exclude every tracked path. Preserve canonical working
outputs, required products/dependencies, acceptance artifacts and immutable evidence.
Only obsolete, unreferenced, reproducible, unused, safe untracked generated EXE/PDB,
temporary binaries and Candidate/Diagnostic outputs are eligible. Check contained
literal paths, ownership, links/hardlinks and use; never follow links outside roots.
Skip uncertainty and report housekeeping failure without reversing publication.
Cleanup is lightweight, idempotent and nonblocking: no full-bin purge, new framework,
service, historical requalification, rebuild, runtime launch or next-phase execution.
For existing seals/completed phases, retain finish-only; never retrofit this contract.

## Exact Milestone C governance bridge

The bounded remediation is recorded in
`docs/pre_editor/active/MILESTONE_C_ROADMAP_BRIDGE_REMEDIATION_01.json`.
It preserves the original roadmap activation and application records as immutable
provenance. Current infrastructure bindings are in the explicit publication
manifest under `docs/pre_editor/revisions/MILESTONE_C_ROADMAP_PUBLICATION_01/`.
The separate `milestone-c-roadmap-01` operation is never a numbered phase approval.
Its `roadmap-check` is read-only; `roadmap-finish` requires separate exact Owner
publication authorization. Neither operation grants START or a technical waiver.

Phase26 baseline admission requires the helper's exact completed bridge guard:
the original Phase25 approved commit/tag/journal/receipt and Physics tree, the
bridge's sole parent, exact commit/tree/annotated governance tag, COMPLETE private
journal, noncircular public receipt, authorization and artifact/source bindings.
The Phase26 seal retains that proof and binds its spec, manifest, receipt and
separate publication authorization. Missing or drifted proof blocks; arbitrary
HEAD ancestry and an ordinary Phase25-to-Phase26 shortcut are not admitted.
Later numbered-phase baselines retain their existing strict checks.

Before any separately authorized Phase26 implementation, verify this baseline and
the distinct joint-entry Owner decisions in
`docs/pre_editor/active/MILESTONE_C_PHASE_26_ENTRY_DECISION_REVIEW_01.md`.
PHY-01, OD-06 and OD-08 remain unresolved. Phase21 remains FROZEN_UNAPPROVED;
Candidate11 coupled-contact modifications remain rejected. Publication cannot
resolve these decisions or substitute for an explicit START instruction.
The READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL handoff and internal
seal/finish ordering remain unchanged. Rendering routing and phase00-25 history
are unaffected. Do not alter bridge governance during numbered-phase execution.
