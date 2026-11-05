# Intended transaction procedure; not executed by this campaign

1. Obtain separate explicit Owner authorization for this exact publication spec
   and manifest. Revalidate the current READY evidence identities and local
   source bindings. Do not infer permission from the implementation authorization
   or the phase publication eligibility grant.
2. Only after that authorization, create the fixed local
   `docs/pre_editor/active/MILESTONE_C_ROADMAP_PUBLICATION_AUTHORIZATION_01.json`
   using the reviewed inactive template. It must have schema1, exact target fields,
   the final spec SHA256 and status `OWNER_AUTHORIZED_PUBLICATION`. Preserve the
   actual Owner instruction separately. This authorization remains local and its
   raw hash is retained in the private publication journal.
3. From the PRE_EDITOR worktree, optionally run the read-only readiness check:

   ```powershell
   python -B .codex/tools/rendering_checkpoint.py --workflow pre-editor roadmap-check --spec docs/pre_editor/revisions/MILESTONE_C_ROADMAP_PUBLICATION_01/publication-spec.json
   ```

4. Under the new explicit authorization, invoke the established checkpoint entry
   point once with the distinct governance operation:

   ```powershell
   python -B .codex/tools/rendering_checkpoint.py --workflow pre-editor roadmap-finish --spec docs/pre_editor/revisions/MILESTONE_C_ROADMAP_PUBLICATION_01/publication-spec.json --authorization docs/pre_editor/active/MILESTONE_C_ROADMAP_PUBLICATION_AUTHORIZATION_01.json
   ```

   It acquires the existing operation lock, revalidates authorization, exact
   local artifacts, approved25 proof, index/state and remote destination/refs;
   journals intent; stages only explicit literal paths; commits with sole parent25;
   creates the distinct annotated governance tag; atomically pushes that branch
   and tag without force; verifies remote identities; records COMPLETE. It never
   seals or approves a phase, writes numbered state or uses a Phase26 tag.
5. If interrupted, retain every journal, index entry and ref. Reinvoke this same
   exact operation only while the same explicit Owner authorization remains
   applicable. Exact COMMIT_PENDING/TAG_PENDING/PUSH_PENDING identity checks
   recover completed side effects. Any changed artifact, authorization, unrelated
   index entry, unexpected ref or state stops for preservation and review. Never
   delete a journal, retag, reset or force-push to make recovery pass.
6. Report the actual local and remote commit, annotated tag object, peeled commit,
   private journal SHA256 and committed receipt identity. Confirm the numbered
   approved25/next26 state and approved Physics identity are unchanged. A successful
   bridge permits only baseline admission; it grants no implementation authority.
   Phase26 still needs separate unresolved joint-entry decisions and explicit START.

Readiness is local/offline. A busy operation lock, stale refs, unavailable origin,
server rejection or conflicting transaction blocks publication. Preserve the
precise error; do not substitute a new transaction mechanism.
