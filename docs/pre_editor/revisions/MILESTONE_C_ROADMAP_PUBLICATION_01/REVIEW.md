# Milestone C governance publication bridge review

The phase checkpoint helper could publish only a numbered sealed phase. A separate
roadmap commit would then fail Phase26's exact Phase25 baseline check. Reusing
phase26 approval to carry governance would invent phase approval; force-adding
ignored active directories would expose unrelated local state. This remediation
adds one fixed non-phase operation and one exact Phase26 bridge admission.

## Identity and scope

The bridge identity is `milestone-c-roadmap-01`; its sole parent must be
`1a2adf61c1571d9f4604f2ca185fea3f95682b4c`. Its distinct annotated tag is
`pre-editor-milestone-c-roadmap-01-approved`. The helper pins the original
Phase25 annotated tag object, completed journal, working receipt hash, committed
receipt blob and approved Physics tree. Raw CRLF receipt and committed LF blob
are intentionally separate preserved historical identities.

The existing checkpoint entry point dispatches to
`tools/pre_editor_roadmap_bridge.py` only under `--workflow pre-editor`. It reuses
Repo/path validation, the existing OS operation lock, atomic JSON writes, clean
index checks, staged/worktree comparison and Git/destination primitives. Ordinary
Git commit honors hooks; there is no commit-tree or hook bypass. Existing phase
seal/finish/publication logic remains in place. Only the prospective Phase26
baseline, sealed proof and exact contract input closure gain the bridge guard.

The publication closure includes all43 accepted roadmap artifacts, finite
governance source copies, the two normal tools files and finite generated review,
procedure, attributes and receipt files. Spec and manifest are added explicitly.
The immutable accepted activation pins the complete roadmap source closure, so a
new authorization cannot silently omit an accepted roadmap capability. The
manifest binds exact local source bytes; archived copies use bounded `-text`
attributes. No ignored source is force-added. The local installation is retained.

## Transaction and preservation

Future explicit Owner authorization must bind the final exact spec. Before any
mutation the helper checks the baseline, incomplete phase transactions, clean
index, exact artifacts, tag collision, destination and remote branch/tag state.
An unrelated staged or unmerged index blocks without edits. Unstaged/untracked
Owner work outside the payload is never staged or restored. Unapproved Physics
changes block governance publication. A later Phase26 candidate can change its
authorized Physics sources; its bridge guard compares the immutable bridge tree.

The private `pre-editor-governance/milestone-c-roadmap-01.json` journal records
PREPARED, STAGED, COMMIT_PENDING, COMMITTED, TAG_PENDING, TAGGED, PUSH_PENDING
and COMPLETE. Pending intent precedes each commit/tag/push boundary. Recovery
admits only the exact expected tree, sole parent, tag object, authorization and
outside-index identity. It neither rolls back work nor invents completion.
Atomic non-force push publishes exactly the named branch and governance tag;
remote annotated tag object and peeled commit must match before COMPLETE.

The public receipt hashes a canonical payload excluding its own entry. The
manifest hashes that receipt; the spec hashes the manifest; future authorization
hashes the spec; the private journal binds these to actual commit/tree/tag objects.
There is no self-hash or containing-commit cycle.

## Limits and retained gates

Preparation and offline tests do not publish this snapshot or exercise real
origin credentials, hooks or server atomic-push support. Those are checked in
the future authorized operation; a mismatch must stop without fallback to an
unguarded mechanism. The exact spec/manifest and later journal, receipt and
annotated tag provide the publication identity, not this narrative status.

Phase25 remains approved, Phase21 remains FROZEN_UNAPPROVED and rejected
Candidate11 remains rejected. The original activation/application records and
all accepted technical artifacts retain their original hashes and wording.
New infrastructure bindings supersede only the named local helper/routing
preimages in the remediation overlay. The current numbered state is not advanced.
PHY-01, OD-06, OD-08 and human RBS acceptance remain separate gates. No Physics
code, build, simulation, Solver implementation or Phase26 START is authorized.
