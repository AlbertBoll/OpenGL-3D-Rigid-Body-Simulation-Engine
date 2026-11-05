---
name: pre-editor-phase-execution
description: Implement one explicit START or REVISE PRE_EDITOR phase, leaving an unsealed owner RBS handoff. Phases 07-58 use compact READY handoffs and finalize through APPROVE; legacy phases 00-04 retain explicit SEAL mode. Excludes approval execution, rejection, Rendering, Physics and ambiguous aliases.
---

# PRE_EDITOR phase execution

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

Select exactly one mode from the explicit owner command for phase 00-58:
START, REVISE, or SEAL. Quoted commands, praise and short aliases select none.
Reuse current AGENTS instructions and load only the selected mode's inputs.
These modes never authorize staging, commit, tag, push, approval or the next phase.

Use `.codex/tools/rendering_checkpoint.py --workflow pre-editor` from
`C:/dev/GEngine-pre-editor`. Keep the helper and all integrity guards unchanged.
Resolve every writer against `docs/pre_editor/active/OUTPUT_PATHS.json`;
inspect new writers and reject escaping/reparse output ancestors. Never inherit
Rendering mutable paths, working directories or environment overrides.

## Bounded validation-infrastructure correction loop (START/REVISE only)

Within an already authorized, unsealed START/REVISE, a failed candidate does not
by itself require another owner REVISE command. Pause dependent validation and
preserve the failed attempt, then use the minimum relevant diagnosis to classify
the cause. Automatically apply the demonstrated minimal correction and continue
only when every condition below is established before editing:

- The cause is confined to validation, a test fixture, harness, observer, evidence
  plumbing, or already-admitted mechanical build infrastructure. A file's location
  or a fixture assertion alone is not proof of an infrastructure-only cause.
- The correction changes no production runtime semantics, public API, architecture,
  ownership or resource contract/behavior, accepted threshold, timeout, workload,
  measurement protocol or acceptance criterion. Correcting an observer must retain
  the accepted measurement boundary and meaning; it cannot make a real failure pass
  by changing what is measured.
- Every changed file and action remains within accepted ownership and the existing
  file/scope budget. Mechanical build changes must already be admitted; remaining
  numeric budget does not authorize a new file or build-policy change.
- Historical failed candidate sources, inputs, raw evidence, results and identities
  remain unchanged. Use a new candidate/evidence destination for each affected
  correction as required; never overwrite or relabel the failed attempt.
- Only affected validation is rerun, with exact source/dependency/option identity
  supporting reuse of unaffected evidence. No failed gate is waived, weakened,
  suppressed or relabeled PASS; any new PASS belongs to the corrected candidate
  and the exact inputs/products actually validated.

For each correction, record the observed failure, demonstrated cause and supporting
source/evidence identities, minimal proposed delta, accepted scope and boundary
assessment, affected validation/reuse map, and predecessor evidence reference in
new provenance before applying it. Freeze the corrected inputs, run those checks,
and reassess any failure under these same conditions. Update only current candidate
pointers; retain historical records. A proved deterministic fixture/harness error
with a minimal in-scope correction continues without a separate owner REVISE.

There is no arbitrary iteration-count limit. Continue for as long as each next
correction independently satisfies the conditions. This supersedes unconditional
stop/no-retry handling only for those demonstrated infrastructure corrections;
it does not authorize unchanged retries to obtain a favorable result, repeated
valid performance samples, a reset of the admitted measurement campaign, or changes
to its workload, thresholds, per-run deadlines or other applicable execution limits.
Record all actual attempts. If required revalidation would change an applicable
accepted limit/protocol, stop for the owner's decision instead.

Immediately stop for owner review when any of these applies:

- Production runtime semantics, architecture, public API, ownership or resource
  contracts/behavior must change.
- Ownership/file scope must expand, or a threshold, timeout, workload, measurement
  protocol or acceptance criterion must change.
- A runtime/performance failure is unexplained or may represent a production defect.
- Multiple plausible corrections require an owner policy choice.
- A failed gate needs a waiver, exception or disposition, or an applicable OD/owner
  decision is unresolved.
- Evidence is insufficient to demonstrate that the next correction is confined to
  the admitted validation-infrastructure boundary.

Existing identity, index, snapshot, evidence-integrity, isolation, seal/finish and
publication conflicts still stop under their original guards. Missing or damaged
historical evidence cannot be reconstructed and represented as the original proof.
This loop grants no production fix, scope expansion, owner-acceptance substitute,
SEAL/APPROVE/REJECT operation, publication or next phase. It does not reopen a seal,
completed phase or approval, change legacy human-before-seal requirements, or alter
the approval/rejection skills. Apply prospectively, including the current Phase 12
execution; historical stop records and approvals remain unchanged.

## Phases 07-58: START or optional REVISE

Use this section for 07-58. Completed Phases 00-06 are immutable; no reopening,
revision or revalidation. Standalone SEAL performs no phase operation in 07-58;
APPROVE owns the internal seal/finish transaction without another owner step.

Read current state, active phase contract, applicable C0/V0/entry decisions and,
for REVISE, current review/ownership. Verify root/branch/baseline and published
immediate predecessor. An existing seal/approval transaction takes finish-only
under the approval skill, not execution. Keep output isolation and toolchain/CRT.
Declare exact bounded ownership and preexisting changes; preserve index and owner
RBS bytes. Implement only this phase and perform its required focused checks.
RBS is the normal acceptance application; other app/test gates require an explicit
active-phase contract clause. On failure, apply the bounded correction loop above;
failures outside that boundary preserve evidence and stop for owner review.

### Compact READY handoff for lightweight approval

Do the phase's validation and evidence preparation here, once. At successful READY,
produce an immutable compact record identified from the current review/spec with:

- phase/candidate ID, READY status, baseline/predecessor and bounded owned paths;
- exact relevant behavior inputs: hashes/modes, presence/absence, required external
  tool/dependency identities, and precise membership scopes where discovery can
  add inputs (build globs, include resolution and loaded asset/settings paths);
- per-check/configuration coverage and PASS summaries, commands/dates, original
  tested product identities and stable references/digests for archived evidence;
- separate declared derived-output roles and canonical RBS raw/index bytes/mode;
- the prepared narrow checkpoint spec and unresolved gates (none at READY).

Derive the input list from the actual active-phase dependency closure and contract.
Do not feed all repository files, entire source-root trees or historical protection
lists to APPROVE. Include indirect providers/headers, validation code, build flags,
consumed generated files, settings/assets/shaders and toolchain/dependency identities
when they affect the checks. Do not omit a relevant dependency merely to reach an
arbitrary file-count or timing target. Use precise discovery scopes to detect new
inputs; an edited build/include definition requires affected-impact analysis.

Keep raw build/runtime logs and older attempts as versioned historical provenance;
verify/archive them when producing READY. The compact record is the approval
attestation and carries their original evidence identities. APPROVE reads that
record and checks current relevant inputs; it does not reconstruct the campaign or
rehash its full archive. Known missing/damaged required provenance still blocks.

Keep the existing helper schema. Populate checkpoint_files with exact phase-owned
changes and full canonical RBS. Populate validation_input_hashes with the relevant
behavior closure, compact READY/acceptance evidence and required active contracts.
local_governance contains only the current compact review/spec/READY/approval
records, not every historical log, script, object or previous candidate record.
protected_files covers explicitly at-risk work and the small existing helper/
policy/grant/rejection boundaries, not a recursive repository preservation list.
Reference other historical evidence by recorded identity; never rewrite it.

Exclude generated logs and derived build products/intermediates from behavior
inventories and the publication snapshot by default. Their changes do not block
APPROVE. A current artifact fingerprint needs a specific active-contract reason;
record it separately from originally tested binaries. Raw/index canonical RBS,
relevant consumed inputs and exact checkpoint source are never exempted.

Update only current candidate pointers. Report READY FOR OWNER RBS ACCEPTANCE;
APPROVE WHEN FINAL with RBS configuration/path/runtime cwd/assets and inspection
criteria. The owner may edit/rebuild/run before APPROVE; no mandatory rebuild,
counts, routine REVISE or SEAL. Owner edits make old evidence historical for those
bytes; APPROVE either reuses matching checks or runs the minimum affected checks.
START/REVISE never seals, stages, commits, tags, pushes, approves or starts
another phase.

## Phases 00-04 only: retained execution modes

## START or REVISE: automated validation

Read `docs/pre_editor/active/REFACTOR_STATE.md`, the requested
`docs/pre_editor/phases/PHASE_XX.md`, C0/V0 and applicable decision/gate
sections of `docs/pre_editor/VALIDATION_CONTRACTS.md`. In REVISE, read the
current `docs/pre_editor/active/PHASE_XX_REVIEW.md` and owner findings first.
Load other plan/source references only for a concrete dependency.

Verify root, branch, full baseline and predecessor publication from Git and the
completed journal. Phase 00 uses the pinned Rendering Phase 68 predecessor and
separate plan-acceptance/setup authorization; do not create/recreate a manually
supplied worktree. Phase 01 onward requires the immediately preceding PRE_EDITOR
tag and COMPLETE journal with matching commit and both publication flags.
Stop on identity mismatch or unfinished approval. Resolve only decisions due at
this entry; leave later choices at their gates.

Record exact change members within the phase budget, pre-existing changes,
raw/logical index identity and protected fingerprints before edits. A scope
amendment must be explicit and recorded separately from immutable phase files.
Respect actual ignore/tracking rules; no force-add. Preserve/isolate overlapping
owner edits. Keep toolchain/CRT/dependencies and C0 intact.

An unresolved final sealed candidate is frozen until APPROVE/REJECT; START/REVISE
must stop before edits or validation. Only the pinned original pre-human-order
Phase 04 seal may use the bounded legacy REVISE migration below after governance
acceptance and an explicit REVISE command. Its exact-unchanged SEAL recovery
remains separately authorized. Neither path applies to a replacement seal.
Preserve old reviews/specs/receipts,
journals, evidence and retention refs as provenance; never alter/delete them to
bypass guards. An approval transaction or approved history blocks ordinary
revision. Every new revision must pass the automated handoff and owner human
gate again; an old receipt does not approve the new candidate.

Implement only this phase. Phase 00 permits focused setup/routing/isolation/hash
and protected/index checks only; no agent builds, tests, formatting, application
or benchmark runs. For later phases, select the smallest required automated
validation from the active contract and V0. Application execution is RBS only.
Do not inherit Rendering's historical multi-app/performance matrix. Record
actual commands, exits, failures, reuse identities and limitations.

Use `fingerprint --paths <exact inputs>` before focused automated validation.
Capture source/configuration inputs before builds and recheck them afterward.
Record the exact built binaries/libraries actually used by each automated run.
Preserve those original inputs/results/identities and archive mutable runtime
logs/settings used as evidence before the owner's run can overwrite them.
Never fabricate PASS or discard failed results.

### Prepare the human handoff; do not seal

Prepare/update the existing `logs/pre_editor/phase-XX/checkpoint-spec.json`
(or current spec referenced by active state during REVISE) with `phase`, full
`baseline`, concise `summary`, `review`, exact `checkpoint_files`, ignored
`local_governance`, `protected_files`, `contract_files`,
`validation_input_hashes`, `findings` and actual `validation` entries.
The active phase and VALIDATION_CONTRACTS remain mandatory contract inputs.
Read Phase 00's spec as a schema example only if needed; never inherit its list.
Classify the spec itself as local_governance so the helper binds its final bytes;
do not put its own hash inside validation_input_hashes (a circular self-hash).

After all required automated validation succeeds, set the current review and
spec's `candidate_status`, and report to the owner, exactly:
**VALIDATED CANDIDATE; HUMAN VALIDATION REQUIRED BEFORE SEAL**.
Mark human validation PENDING. Update only the existing current-candidate
phase/spec/review/scope pointers as needed; leave approved fields unchanged.
State remains subject to the helper's state_before/after guard, not a fixed hash.

Point the spec's `automated_validation` field to a distinct immutable handoff
manifest under this phase's evidence root. Write that manifest last, recording
the completed review/spec hashes without inserting its own hash into those
documents. Use a new path on revision; preserve earlier manifests. Record:

- Phase, candidate identity, root/branch, full baseline/predecessor, exact
  checkpoint/local/protected/contract membership and raw/logical index identity.
- Complete behavior-affecting input fingerprints: production and validation
  source, headers, shaders, assets, fixtures, scenario/settings/acceptance rules,
  scripts, build configuration/projects, compiler/SDK/dependency identities and
  relevant governance. Include transitive input inventories and their contents,
  additions/deletions and modes; a checkpoint-files-only list is insufficient.
- Original automated commands/results/evidence hashes and the exact binaries
  and libraries that received that validation. Keep this evidence immutable.
- An exact, separately classified list of derived build products whose final
  hashes may change after the human rebuild, with configuration and producing
  input closure. Prebuilt dependency inputs are not refreshable build products.
  No wildcard binary-directory exemption or new classification in SEAL.
- The review/spec and human-confirmation metadata paths allowed to be updated
  in SEAL. Freeze their initial identities and the scope/criteria they describe;
  this is permission to annotate human evidence, not to change the candidate.
  Predeclare the human-record path in local_governance and the manifest path in
  final fingerprint membership; these records must exist before the final seal.

The complete canonical `RigidBodySimulation/src/RigidBodySimulation.cpp`
must be a checkpoint member and a pre-validation fingerprint input even without
a source diff. Preserve all existing raw/index blob, mode and retention rules.
Before sealing, this source must match the automated candidate like every other
behavior input; the after-seal owner-mutable mechanism is not a pre-seal waiver.

Give the owner the exact local Visual Studio solution/configuration(s), RBS
executable from that build, asset root/runtime cwd and phase-specific inspection
criteria, including the required build closure defined below. Request the actual
solution result and known failed project details as part of the human report.
The owner must manually perform **Rebuild Solution**, launch
**RigidBodySimulation from that human build**, and complete the required manual
runtime inspection successfully. Other applications are not launched.
This human requirement precedes every new seal, including setup/docs-only
candidates; their agent automated checks remain bounded by their phase.

Stop here. START/REVISE must not invoke `seal --spec`, generate a new eligible
receipt, or report AWAITING HUMAN REVIEW. Wait for the explicit SEAL command.

### Owner edits during pre-seal human validation

Before final seal the owner may rebuild, run RBS, inspect and edit the canonical
`RigidBodySimulation/src/RigidBodySimulation.cpp`. Any owner RBS edit invalidates
the prepared candidate: record STALE CANDIDATE; REVISE REQUIRED, preserve the
owner's bytes and every earlier automated/human evidence record as historical,
and return to explicit `REVISE PRE_EDITOR PHASE XX`. The edit itself is not
rejection or recovery failure. Do not restore/import old RBS bytes to qualify,
or refresh source fingerprints as a substitute for validation. SEAL performs no
revalidation and stops without sealing a stale candidate.

REVISE captures the then-current intended RBS bytes and identifies affected
current-phase behavior, required GEngine/RBS dependencies and focused checks.
Preserve historical evidence in versioned directories. Validate stable final
inputs, reusing only unaffected evidence whose source/build/input/settings
identities and claims still match. A source change during validation invalidates
that attempt; retain its actual results and re-establish stable inputs before
another bounded attempt. A real compiler/runtime failure prevents a new validated
handoff; it is not a reason to restore the owner's edit merely to match the past.

For Phase 04 use C0/V0 and RBS-A-Authoring: affected Debug/Release RBS dependency
builds and affected create/assign/share/duplicate, foreign/stale/wrong-domain,
no-heavy-copy, retained-frame and integration/shutdown checks. Do not require
PhysicsTests or other applications unless an applicable active-phase RBS contract
or affected mandatory boundary gate specifically calls for a compile/link check;
cite it before broadening work. No other application runs or default broad matrix.

After successful focused validation, produce a fresh immutable handoff and stop
as VALIDATED CANDIDATE; HUMAN VALIDATION REQUIRED BEFORE SEAL. Require a subsequent
owner Rebuild Solution, launch of that human-built RBS and accepted inspection.
Earlier human acceptance cannot substitute for this final pass. The owner may
iterate edit -> REVISE/revalidate -> rebuild/run/inspect until satisfied.
Only final source bytes that passed the required automated validation and final
owner inspection may seal. Any further source edit returns to REVISE.

### Mandatory human rebuild-result gate

The gate uses the active phase's required build closure, not an inferred
full-solution success. At the automated handoff, identify GEngine,
RigidBodySimulation, their transitive build dependencies and every additional
project/output required by the active phase contract. Use the phase contract,
solution/project references and shared output dependencies as evidence; an app's
name or ownership alone does not prove it is outside the closure.

Phase 04 has an explicit owner exception: human acceptance requires successful
GEngine/RBS and their required build/runtime dependency closure, RBS launch from
the human rebuild and accepted manual RBS inspection. PhysicsTests is outside
this human gate. Its known Release SDL_GL_GetCurrentContext link failure is
owner-accepted out of scope even if Phase 04 exposed or contributed to it;
do not require proof of non-causation for this named finding. Preserve the
historical actual 8 succeeded, 1 failed result as FAILED, and obtain each later
human pass's actual result rather than copying that count forward. This grants
no PhysicsTests/full-solution buildability claim and waives no required RBS gate
or actually applicable automated phase contract. Other phases retain the normal
closure rule. New/unaccounted failures still require classification.

The separate human record must capture:

- The actual owner-reported Visual Studio solution result/summary, local solution,
  configuration/platform, and succeeded/failed/skipped counts when known.
  Record unknown counts as unknown; do not invent zero failures.
- All known failed project names and reported errors, plus any skipped/unbuilt
  required projects. Reconcile the failure summary with the named failures.
- Required-closure project results and evidence of the closure classification.
  Required GEngine/RBS/dependencies must successfully rebuild; an existing or
  stale executable, a skipped required project or an invoked command is no PASS.
- RBS executable identity from that human build and owner-reported success for
  the required runtime inspection.
- Each proposed non-owning failure, proof it is outside the required closure
  and does not invalidate shared outputs, validation or the phase, and explicit
  owner acceptance of that named exception (which may accompany SEAL).

A failure inside the required closure blocks sealing. So do missing actual
solution results, unidentified/unaccounted failures, uncertain failure scope,
unproven phase validity or incomplete runtime confirmation. Ask for the missing
owner facts; do not infer success from SEAL alone or repeat builds/tests to fill
the gap. SEAL is confirmation of the human work, not an invented build report.

Keep `solution_result` separate from `phase_gate_result` in the human record.
If any solution projects failed, the actual solution result remains FAILED.
A phase gate may pass with explicitly accepted, proven non-owning failures,
or the specifically owner-excluded Phase 04 PhysicsTests finding above.
Never label that outcome an unconditional full-solution PASS.

In receipt validation entries, a PASS must name only the proven successful
required build closure and RBS inspection. When the solution had permitted
failures, also record its actual result as `ACCEPTED_EXCEPTION`, with the
helper's existing `actual_outcome` and `owner_acceptance` fields, the named
failures/classification evidence and the human-record evidence path. Without
specific acceptance, stop. Use full-solution PASS only when the owner actually
reports full success with no failed projects; preserve known counts/skips.
Keep original automated validation entries unchanged.

## SEAL: confirm human validation and seal

Only `SEAL PRE_EDITOR PHASE XX` confirms the mandatory owner human work for
the prepared candidate. Apply the human rebuild-result gate above using the
actual owner report; the command alone does not establish full-solution success.
Reuse supplied facts and consent; request only missing results or specific
exception acceptance, not duplicate confirmation.
SEAL does not authorize production/validation-source edits, rebuilds,
compilation/linking, tests, application runs or broad validation reruns.

Only when separately authorized for the pinned original Phase 04 seal, use the
bounded legacy recovery subsection below to establish equivalent original
candidate provenance before the shared human gate and final sealing steps.
Otherwise apply the normal handoff requirements that follow.

Read current state, the named current review/spec, its immutable automated
handoff manifest and referenced input/evidence inventories, OUTPUT_PATHS and
any existing journal for this phase. Require the prepared pre-seal handoff for
this exact phase/candidate, successful automated evidence and no unresolved gates.
An existing sealed candidate outside the bounded Phase 04 paths, or any
active/completed approval transaction, must not be refreshed or resealed here;
stop and preserve its evidence and journal for APPROVE/REJECT.
For finalization after the bounded legacy REVISE path, use its fresh automated
handoff and subsequent human pass; never compare changed source to an old
candidate and merely accept refreshed hashes.
For an unsealed candidate, a missing
or incomplete handoff requires `REVISE PRE_EDITOR PHASE XX`.

Before writing any final metadata:

1. Verify root/branch/baseline/predecessor and current candidate pointers. Verify
   raw/logical index identity including stages/modes/flags and protected work.
   Check the review/spec still match the automated handoff and its declared
   scope, criteria and allowed output paths.
2. Compare the complete behavior-input closure and original automated evidence
   against their recorded fingerprints, including the canonical RBS raw bytes.
   Changed/missing inputs, changed evidence, added source, type/mode changes or
   incomplete provenance stop seal. An owner RBS edit makes this pre-seal candidate
   stale and routes to focused REVISE without rejection; damaged provenance or
   other unclassified drift remains an integrity blocker. Do not bless changes
   by recomputing hashes, import older RBS bytes to conceal drift, or widen the
   refreshable list.
3. Identify differences in the predeclared derived build products only. Require
   their expected paths/types/modes, unchanged producing inputs/toolchain/
   configuration and the owner's confirmation. Missing products or unclassified
   differences block sealing. Merely finding an executable proves no test ran.

After these checks, record the exact SEAL instruction and confirmation time,
known configuration/executable identities, the actual owner-reported solution
result/failed projects, required-closure findings, inspection and any accepted
exceptions in a separate current human-validation record, for example
`docs/pre_editor/active/PHASE_XX_HUMAN_VALIDATION.json`. Do not invent a build
command log, process exit, actual build time or observation not supplied by the
owner. Include this record in the final local_governance and fingerprint inputs.

Update only the permitted review/spec and final identity metadata. Keep original
automated validation entries, evidence and tested-binary hashes intact. Add
distinct `validation` entries using the human rebuild-result gate above:
scoped PASS for successful required-closure rebuild/RBS inspection, and
ACCEPTED_EXCEPTION with actual_outcome/owner_acceptance for any admitted
solution failures. Use explicitly owner-reported `command` descriptions and
the human-record `evidence` path, not an agent-executed test claim.
The helper carries validation entries into the receipt; custom spec fields
alone are not sufficient evidence.

Use `fingerprint --paths <exact final inputs>` to refresh only declared final
build-product and permitted metadata hashes in `validation_input_hashes`.
Keep behavior/evidence fingerprints unchanged and bind the immutable handoff
manifest itself. Preserve original automatic and final human-build binary
identities separately. A changed post-human-build binary has human validation
only; never relabel the earlier automated run as testing it. Matching automated
evidence may be attributed only to the exact binary that actually received it.

Prepare review/spec status as HUMAN VALIDATION CONFIRMED; SEAL PENDING. Then
invoke the existing operation once, from the verified PRE_EDITOR root:

```powershell
python -B .codex/tools/rendering_checkpoint.py --workflow pre-editor seal --spec <current-spec-path>
```

Do not bypass any helper guard. It preserves Phase 00's pinned original package
bytes and the full canonical RBS raw/index blobs/mode/retention refs; imported
validated bytes still require the prior seal's exact identity. Seal writes the
receipt and private journal without staging or publication. On failure preserve
the candidate/evidence and report the blocker; do not retry or repair in SEAL.

Only successful sealing transitions to **AWAITING HUMAN REVIEW**. The SEALED
journal and eligible receipt establish that transition; report them with the
review and both evidence sources. Do not edit the sealed review/spec/state to
record success, since those bytes are already bound by the seal.

### Bounded legacy recovery of the original Phase 04 seal

This is one conditional path within SEAL, not another mode or a general reseal
permission. It requires both the exact `SEAL PRE_EDITOR PHASE 04` command and
separate explicit owner authorization to recover the original Phase 04 seal
identified below. A bare SEAL command, earlier APPROVE, governance acceptance,
or instruction to revise this governance does not authorize recovery.

Only these original identities qualify (SHA-256 of complete raw files):

- Baseline: `a56761f7e2096af4937ad2c9750b01e8591a0014`.
- Phase 04 private journal: `eb018dc4b60ae250462e0924fd3a686d40a1290eefc5e1b554493e49c6467d70`.
- `pre-editor-checkpoints/phase-04.json`: `3eca1ac1b62f4e0a7c49412f6e0fb3bfa118babd1ba4de4510b62255553a6c9c`.

Before any candidate metadata edit, prove all of the following read-only:

1. The exact original journal/receipt identities still match; journal status is
   SEALED and transaction is null. State/HEAD/predecessor remain at approved
   Phase 03, with 04 next. Verify the original index identity and protected work.
2. No Phase 04 approval commit, tag or publication exists. Check local refs,
   receipt history and relevant reflogs, the original journal, and read-only
   remote branch/tag identities at the unchanged configured destination. The
   remote refactor branch must still equal the Phase 03 baseline and the Phase
   04 approval tag must be absent locally/remotely. Missing/ambiguous evidence
   or unavailable remote verification blocks recovery; never derive consent or
   rewrite policy from discovery.
3. Production/validation source, contracts, settings, build configuration,
   dependencies/toolchain and all other behavior-affecting inputs still equal
   the previously automated candidate, including canonical RBS raw bytes,
   additions/deletions and modes. Establish their old identities from the
   original seal, baseline and original automated inventories/evidence, never
   from current bytes alone. Original automated evidence and tested-binary
   identities must remain intact. Missing coverage or non-RBS input/evidence
   drift blocks this exact-unchanged path. An owner canonical RBS edit instead
   makes the old candidate stale and requires the bounded legacy REVISE path
   below; do not rebuild, rerun or reimplement inside SEAL to qualify.
4. Enumerate every difference from the old seal. The only permitted derived
   artifact differences are these specifically identified human-rebuild outputs:
   `bin/Release/GEngine/GEngine.lib`,
   `bin/Release/RigidBodySimulation/RigidBodySimulation.exe`, and
   `external/glad/bin/Release/glad/glad.lib`. Prove unchanged producing inputs
   and expected types/modes. These names alone are not proof of provenance.
   The only other permitted differences are separately owner-authorized
   governance metadata: the reviewed AGENTS/execution-skill correction and its
   governance review/evidence (including the earlier rejection compatibility
   edit). Verify exact paths, before/after hashes and authorization against the
   accepted governance diff; no helper, policy, contract, source or settings
   exception. Current phase review/spec must still match the old candidate until
   the permitted recovery annotations below. Unclassified differences stop.

Before replacing anything, preserve byte-exact copies of the old receipt,
private journal, current review/spec and original identity/evidence inventories
in a new, non-overwriting recovery provenance directory under the Phase 04
evidence root. Verify copy hashes against originals; preserve original evidence
and raw/index blob identities, modes and all durable retention refs. Do not
delete/edit the old journal to make the helper accept recovery.

Use this original sealed provenance in place of the new-workflow automated
handoff only for this recovery. Record the eligibility proof, exact difference
inventory, owner authorization and original hashes in a recovery manifest;
do not backdate it or claim fresh automated validation. Apply the human-result
gate above, including the actual solution result, failed projects and any
explicitly accepted non-owning exceptions. Reuse the already completed human
rebuild/inspection only when the owner confirms it produced and inspected the
current final artifacts for this unchanged candidate; recovery implies no new
build. Missing owner results block recovery.

Then update only the current review/spec and separate human/recovery metadata.
Retain the exact production checkpoint membership and original automated
entries/results/tested hashes. Fingerprint current final human-build artifacts
and authorized metadata only; keep behavior/evidence hashes unchanged.
Bind the preserved old receipt/journal/review/spec copies, recovery manifest
and human record in the new spec's local governance/fingerprint inputs, not
only custom spec fields. Add a `legacy_recovery` record identifying the exact
old seal and one-time replacement; keep actual automated and human identities
separate. Resolve new output paths under the existing output-path rules.

Recheck the original journal/receipt identity, no transaction/publication,
unchanged inputs/index and final human gate immediately before invoking the
same existing PRE_EDITOR `seal --spec` operation once. No manual journal
retirement, weakened check, helper change or publication authorization is used.
On failure stop without retry or repair. On success the new immutable eligible
seal is AWAITING HUMAN REVIEW and consumes this exception: its identity differs
from the pins above, so it cannot qualify again. Normal freeze and the unchanged
single APPROVE finish transaction with every snapshot guard apply thereafter.

### Bounded legacy Phase 04 return to REVISE

This extends only the original Phase 04 pins above, under Governance Revision 02
owner acceptance and explicit `REVISE PRE_EDITOR PHASE 04`. It is part of existing
revision mode, not another skill/mode, rejection/reimplementation requirement,
or permission to reopen any final/replacement seal. Governance preparation or
acceptance alone runs nothing. For the current case the owner selected this
revision path; earlier SEAL consent does not activate work or final sealing.

At first migration entry prove the pinned SEALED/null-transaction journal/receipt,
Phase 03 baseline and completed publication, no Phase 04 approval commit/tag/
publication locally or remotely, correct index/destination and intact original
evidence/raw/index blobs/retention refs. Exhaustively compare original inputs.
Owner edits to exactly the canonical RBS source are intended revised inputs.
They invalidate old candidate currency without being recovery failure. All other
source/validation/contract/settings/toolchain/dependency identities must match
the original, apart from the already identified three human-build products and
explicitly authorized governance/recovery metadata. Other unexplained differences
or missing/damaged provenance still block. Preserve the owner's current bytes.

Verify the already archived original provenance and archive current review/spec/
human records before replacing any current-candidate metadata. Preserve the
blocked-attempt records exactly as historical observations; do not rewrite them
as current validation. Use a new versioned phase evidence directory and local
migration/revision manifest with original pins, current RBS hash/diff, difference
classifications, authorization and ORIGINAL SEAL HISTORICAL ONLY; REVALIDATION
REQUIRED. Keep the original receipt/private journal unmodified until final seal;
do not retire/delete them or alter their status/transaction.

During this authorized REVISE, point only existing current-candidate state fields
to the versioned new spec/review, keeping approved fields, baseline and policy
unchanged. These legitimate new pointers differ from the old state_before:
the unchanged finish snapshot guard prevents stale approval. Do not restore old
pointers to revive the obsolete seal. No finish/check operation is invoked by
this path. A failure retains the revision state/provenance; it does not auto-reject.

Perform the affected Phase 04 RBS-centered revision/validation described above
against current final bytes. Declare the revision's actual build/evidence output
paths before writing them; newly authorized outputs are not exceptions to an old
snapshot. Preserve original evidence in place and use new evidence paths for
every attempt. On success record the new complete behavior/evidence closure,
tested binaries, exact derived build products permitted during the next human
pass, and versioned handoff. Bind original provenance and revision lineage in
the final spec's local governance and fingerprint inputs. No seal from REVISE.
Further owner edits repeat this loop with new evidence/human validation. On later
REVISE iterations, retain the original pins/no-approval proofs but classify input
and output differences against the latest revision handoff and its declared
outputs. Do not mistake products from authorized focused REVISE for unexplained
old-seal drift, or treat undeclared non-RBS changes as owner-edit permission.

For the eventual explicit SEAL, first recheck the pinned original journal/receipt
and absence of approval/transaction/publication; otherwise stop. Compare current
source/behavior/evidence with the latest successful new handoff, not old source.
Require the final owner rebuild/run/inspection after that validation and the
actual human result under the Phase 04 RBS-only gate. An RBS edit returns to
REVISE; no broad validation in SEAL. Finalize only permitted metadata/derived
fingerprints, with distinct automated versus human binary attribution.

Invoke the same existing seal once, using the newly validated canonical source
directly (no old validated_source import). The helper already admits the pinned
unapproved SEALED/null-transaction predecessor and creates the new canonical raw/
index blobs and content-addressed refs without deleting old refs. Do not change
the helper, receipt eligibility flags manually, approval behavior or snapshot
guards. After success the new seal is AWAITING HUMAN REVIEW, consumes the legacy
exception and freezes all candidate bytes until APPROVE/REJECT.

## After sealing

The candidate is frozen until APPROVE/REJECT: no builds, rebuilds, tests,
application runs, evidence regeneration or candidate modification. START,
REVISE and ordinary SEAL cannot reopen it. Only the pinned original Phase 04
seal has the separately authorized migration paths above; a replacement seal
has no such exception. Source editing after final seal is prohibited. Preserve
unexpected owner edits under the existing protections; never weaken the helper's snapshot mismatch detection.

APPROVE remains the approval skill's single `finish --phase XX` transaction,
with no rebuild, rerun, refreshed hashes or reseal. Only explicit approval
activates publication. Never advance approved state or start the next phase.

## Phases 18-58: Minimum Sufficient Validation and mandatory human handoff

For an explicitly authorized, unsealed START/REVISE after recorded owner acceptance
of the governance, read `docs/pre_editor/active/PHASE_18_PLUS_MINIMUM_SUFFICIENT_VALIDATION.md`
and the existing retention supplement with C0/V0 and only the active phase contract.
This addendum controls prospective selection/disposition and handoff where older
campaign language conflicts; it does not alter technical gates, scope/budgets,
helper guards, completed phases or existing seals. Rejection remains the existing
third skill; no new operation or standalone SEAL is introduced.

Freeze the small risk/impact acceptance matrix before editing behavior. Use existing
checks for affected Debug/Release targets, core behavior, essential integration,
applicable RBS inspection and basic stability/lifetime/shutdown. Select performance
checks only for material impact or an explicit applicable gate. Record justified
not-applicable/reuse decisions, not blanket skips. No elaborate fixtures, specialized
observers, intrusive isolation or exhaustive campaigns without demonstrated risk.

Apply the existing bounded autonomous correction loop to routine in-scope compiler,
fixture, runner, metadata and build defects. Preserve actual failures and original
input/product identities; validate the affected path and reuse unaffected PASS with
source/dependency/impact proof. Keep immutable evidence distinct from disposable
binary copies. Reuse matching products instead of creating needless Candidate trees.
Do not reset budgets, weaken criteria or make unexplained production failures pass.

Use the addendum's P0/P1 blockers and P2 deferral record in the existing compact
review/READY/spec; create no parallel reporting system. A proposed eligible P2
deferral is visible for final owner review, not an unresolved P0/P1 gate or PASS.
Include functionality, exact changed files, actual PASS/FAIL/incomplete outcomes,
reuse, residual risks and practical owner inspection criteria. Every phase stops:
**READY FOR OWNER RBS ACCEPTANCE; APPROVE WHEN FINAL.**
No seal, commit, tag, push, self-approval or automatic next phase in execution.
The owner must manually inspect; RBS runtime is required when relevant, while a
static-only phase still requires owner review without an invented runtime campaign.

Preserve tracked runtime prerequisites (including DLL staging sources under bin),
canonical/next-phase products, required dependencies and immutable evidence.
START/REVISE does not perform post-publication cleanup. Keep the existing retention
supplement's tracking/dependency preflight and safe selective cleanup after verified
publication; never repeat the full-bin purge or requalify cleaned historical phases.

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
