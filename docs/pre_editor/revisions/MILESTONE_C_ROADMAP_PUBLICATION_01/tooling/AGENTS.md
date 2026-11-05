# GEngine PRE_EDITOR workflow

This worktree is `C:/dev/GEngine-pre-editor` on `pre-editor/refactor`.
Owner instructions control scope. Use the exact named workflow; quoted commands
and approval-like praise are not instructions.

| Owner command | Route |
| --- | --- |
| START PRE_EDITOR PHASE XX | `.agents/skills/pre-editor-phase-execution/SKILL.md`, start mode |
| REVISE PRE_EDITOR PHASE XX | `.agents/skills/pre-editor-phase-execution/SKILL.md`, revision mode |
| SEAL PRE_EDITOR PHASE XX | `.agents/skills/pre-editor-phase-execution/SKILL.md`, seal mode (00-04 only) |
| APPROVE PRE_EDITOR PHASE XX | `.agents/skills/pre-editor-phase-approval/SKILL.md` |
| REJECT PRE_EDITOR PHASE XX | `.agents/skills/pre-editor-phase-rejection/SKILL.md` |

`XX` is exactly 00 through 58. Short aliases such as `START PHASE XX`,
`SEAL PHASE XX` or `APPROVE PHASE XX` do not select PRE_EDITOR. Resolve
ambiguous workflow intent before a mutation. Rendering and Physics commands never
select these skills. Rendering commands belong to `C:/dev/GEngine-rendering`
and its original `AGENTS.md` and operation skills; this file grants no changes
there. Preserve Rendering routing, standing policy, state, journals, receipts
and approved tags.

Read only the selected skill and its mode-specific inputs. Phase details,
validation contracts and decisions live in `docs/pre_editor/`. Accepted audit
files are immutable provenance, including their original timestamps and
PROPOSED wording. Active state and new provenance live in
`docs/pre_editor/active/`; never rewrite observations to imply current validation.

## START/REVISE: bounded validation-infrastructure autonomy

Within an already authorized, unsealed execution, use the execution skill's
condition-bounded corrective loop for demonstrated validation/fixture/harness/
observer/evidence-plumbing or already-admitted mechanical build failures. The
minimal correction must stay within accepted file ownership and budget and leave
production semantics, APIs, architecture, ownership/resource behavior and all
accepted thresholds, timeouts, workloads, measurement protocols and criteria
unchanged. Preserve each failed candidate, use new correction/evidence destinations
as required, and rerun only affected checks with identity-based reuse of the rest.
Never waive, weaken or relabel a failed gate. No arbitrary iteration-count limit
or separate REVISE command is required merely because the prior candidate failed.

Stop for owner review whenever those conditions cannot be demonstrated, production
behavior may be involved, scope or acceptance policy must change, multiple plausible
corrections need an owner policy choice, a gate needs waiver/exception/disposition,
or an applicable OD/owner decision remains unresolved. Existing seal/finish,
publication, snapshot, evidence-integrity and owner-decision guards stay binding;
this rule applies only to START/REVISE, not approval, rejection or sealed recovery.
It starts no new phase. The prospective activation includes current Phase 12;
historical candidates and approvals remain unchanged. Activation and conflict
resolution are recorded in
`docs/pre_editor/active/VALIDATION_INFRASTRUCTURE_AUTONOMY_REVIEW.md`.

## Phases 07-58: lightweight READY approval

Normal owner flow is `START PRE_EDITOR PHASE XX` -> owner RBS review/edit/rebuild/run
-> `APPROVE PRE_EDITOR PHASE XX`. START performs the active phase's required checks
and leaves READY FOR OWNER RBS ACCEPTANCE with a compact, immutable handoff.
REVISE remains optional for agent changes. No extra owner command, SEAL command,
confirmation, full-solution Rebuild PASS or build-count report is required.

APPROVE accepts the current RBS bytes and owner's manual result. For an unsealed
READY candidate, inspect only the expected phase/candidate, its compact READY
record and explicitly relevant source/input closure, and immediate Git/index/
branch/destination/tag conflicts. Reuse the successful READY evidence when these
inputs match. Do not rebuild Debug/Release, rerun tests, launch RBS, walk broad
repository/history inventories, hash unrelated protected files, recursively verify
old evidence trees, reconstruct campaigns or regenerate preservation manifests.
Normal Git changed-path/index/ref checks are transaction guards, not a request
for a repository-wide content audit. Let the helper enforce its own guards;
do not duplicate them with separate routine check/publication-target passes.

Prepare the narrow input/coverage list during START from the active contract and
actual dependency impact. Include canonical RBS, relevant production/validation
sources, build definitions, consumed generated inputs, settings/assets/shaders,
and required toolchain/dependency identities. Do not use all tracked files, all
source-root contents or historical protected inventories as approval inputs.
Verify content/membership/modes only in that declared closure, including additions
in the exact build/include/asset discovery scopes. Work scales with relevant
inputs, not repository/history size; constant-time proof of arbitrary changed
bytes is not promised. Do not substitute timestamp-only trust or a manifest hash
for comparison of the current relevant inputs.

Keep derived executables/libraries/intermediates, timestamps and generated logs
separate by actual role. Owner rebuild/output changes alone neither block the
fast path nor trigger validation. Do not hash those outputs routinely or put them
in the source publication snapshot unless the active contract specifically needs
a current artifact identity. Keep original tested binary identities as historical
evidence; never claim an untested owner-rebuilt binary was tested previously.
Consumed generated headers, third-party/runtime dependencies and relevant settings
remain behavior inputs regardless of extension or location.

On a match, use one compact owner-acceptance/reuse record and final review/spec,
referencing the READY evidence identities without reopening its raw campaign.
The existing internal metadata seal is retained solely to bind the exact checkpoint;
then immediately run guarded finish once: commit -> annotated approved tag -> push
branch/tag -> verified COMPLETE. There is no additional owner step or validation
campaign. No edits, builds, runs or evidence writes between seal and finish.

If relevant inputs changed, leave the fast path, map the delta to the active phase
contract and run only required affected validation. Reuse unaffected matching
checks; do not default to another complete Debug+Release campaign. C0/V0 technical
gates remain binding. RBS and its required GEngine/runtime closure are the normal
gate; PhysicsTests or another app requires an explicit active-phase contract gate.
Failed checks, known damaged/missing READY provenance, unclassified relevant
changes or publication conflicts stop before seal/publication. No repair, rollback,
source restoration, scope expansion or new owner confirmation is implied.

Keep snapshot/raw-blob/index/protected-work and destination/ref/tag/publication
guards unchanged for the scoped checkpoint. Existing seals/interrupted approvals
always use finish-only; never shrink or refresh an existing seal. This revision
is prospective for 07-58 only. Completed Phases 00-06 and their evidence, receipts,
journals, commits, tags and publications remain immutable. The retained ordering
below applies only to 00-04 and grants no reopening of completed work.

## Phases 00-04: retained human-before-seal ordering

START/REVISE handles exactly one authorized phase, implements it and performs
required automated validation. Prepare the current review/spec and immutable
automated-candidate input/evidence identities, then stop with
**VALIDATED CANDIDATE; HUMAN VALIDATION REQUIRED BEFORE SEAL**.
START/REVISE never invokes `seal --spec`.

The owner must then manually perform Visual Studio **Rebuild Solution**, launch
**RigidBodySimulation from that human build**, and complete the required manual
runtime inspection successfully. Record the actual owner-reported solution
result, including known failed projects. The required GEngine/RBS and active-phase
build dependency closure must pass. Other project failures may be non-blocking
only with proof they are outside that closure and do not invalidate the phase,
plus recorded owner acceptance; otherwise stop. A failed overall solution result
must stay recorded as failed, with any admitted exception distinct from the
successful phase-scoped gate. Invocation alone is not human validation PASS.
Before final seal, the owner may rebuild, run, inspect and edit the canonical
RigidBodySimulation source. An owner RBS edit makes the prepared candidate stale:
preserve the new bytes and historical evidence, then require explicit
`REVISE PRE_EDITOR PHASE XX` for affected focused validation. It is not itself
rejection or recovery failure. Never restore the old source to qualify for seal.
A new validated candidate requires another owner rebuild/run/inspection; repeat
this loop until the final source passes both automated and owner human gates.

For Phase 04, human acceptance is RBS-only, including successful required
GEngine/build/runtime dependencies. The owner accepts the known PhysicsTests
Release SDL_GL_GetCurrentContext link failure out of scope even if Phase 04
contributed. Preserve actual failed solution counts; never claim PhysicsTests
or every solution project is buildable. Automated Phase 04 work remains RBS-
centered under its unchanged contract; other-app compile checks need a specific
applicable gate, and no other application runtime is admitted.

Only explicit `SEAL PRE_EDITOR PHASE XX` confirms the human work and selects
SEAL mode; missing actual results still block. SEAL verifies unchanged
behavior-affecting source and validation inputs;
drift requires `REVISE PRE_EDITOR PHASE XX`. It performs no production or
validation-source changes, builds, compilation/linking, tests, application runs
or broad validation. It refreshes only predeclared final build-product and
permitted metadata fingerprints, records owner confirmation separately from
automated evidence, and invokes the existing `seal --spec` once. Never claim
an earlier automated run tested a changed human-build binary.

Only successful sealing is **AWAITING HUMAN REVIEW**. After sealing, no builds,
rebuilds, tests, application runs, evidence regeneration or candidate modification
are permitted before APPROVE/REJECT. The sole exception is the execution skill's
bounded migration of the specifically pinned original unapproved Phase 04 seal.
Exact-unchanged recovery still requires its existing proofs. Owner-edited RBS
instead returns through the skill's bounded legacy REVISE path, preserving the
original seal/evidence as historical provenance and establishing fresh validation
and human inspection before a final replacement seal. It does not permit general
post-seal mutation. Governance acceptance alone performs no phase work; explicit
REVISE or SEAL selects the authorized path. After replacement, normal freeze
applies without that exception.
No staging, commit, tag, push, self-approval or automatic next
phase in execution modes. Normal approval consumes the eligible seal without
routine builds/tests/runs.

## Shared identity, publication and isolation guards

The owner standing grant is recorded in
`docs/pre_editor/active/PUBLICATION_GRANT_MILESTONE_C_01.json` (the accepted prospective extension; original grant remains immutable) and implemented by
`docs/pre_editor/active/PUBLICATION_POLICY.json`: repository
`AlbertBoll/OpenGL-3D-Rigid-Body-Simulation-Engine`, remote `origin`, branch
`pre-editor/refactor`, annotated tags `pre-editor-phase-XX-approved`, phases 00-58.

Only explicit `APPROVE PRE_EDITOR PHASE XX` activates that grant for the named
phase: in 07-58 publication is conditional on applicable matching PASS evidence
or successful affected validation and seal; existing seals/completed approvals
(including 00-06) use the unchanged finish-only path.
The unchanged helper resolves standing policy and performs integrity/publication
checks internally. After sealing, invoke `finish` once without separate routine
`fingerprint`, `check` or `publication-target` calls or evidence regeneration.
No duplicate destination consent is needed while the grant applies. That approval
authorizes exact checkpoint staging, its commit, annotated tag, branch/tag
publication, remote verification and workflow completion. Governance acceptance
is not phase approval. START, REVISE, SEAL, REJECT, quoted commands and general
praise never authorize publication.

Stop on seal/index/destination mismatches, unresolved gates or a revoked or
inapplicable grant. Do not rewrite policy or derive authorization flags from Git
discovery to bypass a mismatch. Rendering consent is not PRE_EDITOR consent.
Preserve unrelated work and later owner edits. No force-push, tag replacement,
hook bypass or automatic next phase. Resume only the same approved checkpoint.

Use `.codex/tools/rendering_checkpoint.py --workflow pre-editor` explicitly.
It reuses the Rendering checkpoint implementation with separate PRE_EDITOR
state, receipts, journals and retention refs. Never run the Rendering operation
namespace for a PRE_EDITOR phase. The canonical owner-mutable checkpoint member
is exactly `RigidBodySimulation/src/RigidBodySimulation.cpp`: retain its full
raw/index blobs, mode, fingerprints and durable refs even when unchanged.
At final seal its bytes must match the latest successfully automated candidate
and final owner inspection. Pre-seal owner edits require new focused validation;
after final seal source editing is prohibited. Existing owner-edit preservation
protects unexpected data without authorizing post-seal mutation or weaker guards.

For phase execution and pre-seal approval input comparison/affected validation,
apply C0/V0 from `docs/pre_editor/VALIDATION_CONTRACTS.md` and only the active phase.
In 07-58,
RBS is the normal acceptance application; another application/test is a gate only
when the active phase contract explicitly requires it. In 00-04, retain RBS-only
application runtime and other-app compile-only checks for required affected gates. Before any phase writer,
resolve its paths against `docs/pre_editor/active/OUTPUT_PATHS.json`;
do not inherit Rendering working directories, mutable outputs or environment
overrides. Phase 00 START/REVISE permits focused setup/routing/isolation/hash/
protected-index checks only. No agent production changes, formatting, builds,
tests, launches or benchmarks. The mandatory human rebuild-before-seal rule
above is retained for phases 00-04 only.

The original six-file Phase 00 governance scope remains provenance in
`docs/pre_editor/active/PHASE_00_SCOPE.json`. This routing and human-before-seal
ordering supersede the earlier automatic-seal ordering without changing the
remaining phase contracts or helper guards. The correction is recorded in
`docs/pre_editor/active/HUMAN_VALIDATION_ORDER_REVIEW.md`; ordinary approval
does not load either record.

For phases 07-58 the lightweight READY ordering above supersedes the broader
approval inventories used in 06. Active governance review:
`docs/pre_editor/active/PHASE_07_PLUS_FAST_APPROVAL_REVIEW.md`.
Earlier Phase 05/06 governance reviews and all completed Phase 00-06 records remain
unchanged historical provenance. Governance activation does not start Phase 07.

## Phases 18-58: Minimum Sufficient Validation and owner approval

Effective only after explicit owner acceptance and application of review package
`docs/pre_editor/proposals/PHASE_18_PLUS_MSV_REVIEW_01/`, recorded by
`docs/pre_editor/active/PHASE_18_PLUS_MSV_ACTIVATION.json`. An unapplied patch,
governance proposal or Phase17 acceptance never starts Phase18 or approves a phase.
For an unsealed phase 18-58, read the validation and approval addendum
`docs/pre_editor/active/PHASE_18_PLUS_MINIMUM_SUFFICIENT_VALIDATION.md` alongside
C0/V0 and only the active phase contract. Its prospective rules control validation
selection, risk disposition and human-before-publication ordering where earlier
campaign/proposal wording conflicts. Technical invariants, explicit applicable
mandatory gates, accepted thresholds and all integrity guards remain binding.
Completed phases 00-17 and existing seals retain their original finish-only rules.

Freeze a small risk-based acceptance matrix before implementation; this is phase
planning, not an extra owner approval stage. Validate affected Debug/Release
targets, changed core behavior and essential integration; inspect RBS when
applicable, basic stability/resource lifetime/clean shutdown, and performance only
when materially affected or explicitly required by the active contract. Reuse
unaffected PASS with relevant source/dependency/impact proof. Documentation-only
work uses static checks; mechanical changes need affected compilation, not an
invented runtime campaign. Prefer existing tests and deterministic checks.

P0 crashes, memory corruption, ownership or integrity failures always block,
regardless of rarity. P1 core failures require focused checks and block unless
specifically dispositioned by the owner under the addendum; ordinary APPROVE is
not an implicit P1 waiver. Bounded P2 risks may be proposed for deferral with
impact, likelihood, recovery, evidence, residual owner and a revisit trigger.
Disclose them in the final review for owner acceptance. Never relabel FAIL or
incomplete evidence as PASS or downgrade an explicit mandatory gate silently.

Routine in-scope compiler, fixture, runner, metadata and build repairs remain
autonomous under the existing unsealed correction boundaries. Preserve failures
and rerun only affected checks; no full qualification restart for a proven
mechanical error. Do not add elaborate fixtures, specialized observers, intrusive
desktop isolation or exhaustive cases without demonstrated material risk. Stop at
sufficient evidence; do not continuously expand the acceptance matrix.

Every phase stops after necessary automated checks and a concise functionality,
changed-files, PASS/FAIL/incomplete/reuse and residual-risk review:
**READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.**
The owner manually inspects the final candidate, including RBS when applicable.
Only explicit `APPROVE PRE_EDITOR PHASE XX` authorizes the unchanged internal
seal -> finish -> commit -> annotated tag -> push -> verified COMPLETE workflow.
No self-approval, standalone SEAL, premature publication or automatic next phase.

Apply `docs/pre_editor/active/PHASE_18_PLUS_ARTIFACT_RETENTION.json` only after
successful owner-approved publication for cleanup. Never automatically delete a
tracked runtime prerequisite or unconditionally clear either bin root. Preflight
tracking and runtime/build dependencies; retain canonical working outputs,
next-phase dependencies, acceptance artifacts and immutable evidence. Remove only
safe obsolete, unused, unreferenced, reproducible untracked generated outputs.
Prefer matching-product reuse; housekeeping is lightweight, idempotent and
nonblocking, with no new cleanup framework/service. Deleting disposable binaries
does not invalidate historical PASS or justify historical requalification.

The one-time full purge is consumed. Its 86 tracked DLL prerequisites were restored
from approved HEAD; the owner confirmed Release build/runtime inspection. This
activation repeats no cleanup, builds or runtime validation. Preserve Phase17's
commit/tag/receipt/publication/evidence and the original risk proposal unchanged.

## Accepted prospective Milestone C roadmap routing

This section takes effect only with the Owner-accepted
`docs/pre_editor/active/MILESTONE_C_ROADMAP_ACTIVATION_01.json` and the exact
reviewed successor files. The review package or unapplied patch has no force.
Governance acceptance starts no phase, resolves no technical entry gate and
never substitutes for an explicit named START or APPROVE command.

For unsealed PRE_EDITOR phases 26-58, the active plan/index/findings/validation
and phase contract are under `docs/pre_editor/active/roadmap/MILESTONE_C_01/`.
Read its `VALIDATION_CONTRACTS.md` and `phases/PHASE_XX.md`, and bind those files
plus the activation record in `contract_files`. Do not fall back to the old
same-number contract when a successor or activation record is missing.
Phases 00-25 retain their original contract paths and dispositions; every existing
seal, interrupted approval and completed checkpoint retains exact finish-only.
Original accepted audit files, original phase contracts and recorded provenance
remain byte-exact. The original master remains provenance; the accepted amendment
controls only its explicitly revised prospective requirements.

Phases 26-29 retain their identifiers. New 30 is Motors, 31 Movers and 32 Book
Conformance and the C exit. Original PRE_EDITOR 30-55 meanings move to 33-58;
this mapping is documentation lineage, never a command alias. For example, an
explicit PRE_EDITOR Phase 30 command names Motors after activation, while the
original lighting Phase 30 contract remains historical and maps to new 33.
D is 33-43, E is 44-54 and F is 55-58. Physics and Rendering workflow commands
and phase numbers are unaffected. No new phase is automatically started.

The original Phase18+ MSV activation, contract and retention records remain
immutable. Their unchanged validation, severity, human-review, correction and
retention rules apply prospectively through 58 under the new activation; original
MSV references to PRE_EDITOR 52/53 performance admission map to new 55/56.
Bind the original MSV/retention records and this narrow extension in current
contracts without refreshing their historical hashes. Legacy ordering is unchanged.
The accepted publication extension covers 00-58 at the same repository, origin,
branch and annotated tag template. Original consent and all approved identities
remain historical. A separate named phase APPROVE remains mandatory.

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
