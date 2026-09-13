# BUILD PHASE XX review

Workflow: AWAITING HUMAN REVIEW or BLOCKED
Phase title / contract:
Human START or REVISE instruction:
Previous approved checkpoint (source tag for Phase 00):

## Scope and behavior

Goal achieved, exact changes and why; explicit non-goals respected. Separate
target/dependency/build changes from any narrowly authorized boundary code edits.
List preparation evidence and blockers without silently expanding the phase.

## Target and dependency impact

Before/after targets, exact TU/header ownership, PUBLIC/PRIVATE/INTERFACE decisions,
cycle analysis, package/profile/SDK changes and link/runtime closure. For documentary
phases explicitly state no graph behavior change.

## Validation and Premake reference comparison

| Exact command | CWD / config / tool/environment identity | Result / exit | Evidence path and SHA-256 |
| --- | --- | --- | --- |

Record all contract mandatory checks, with PASS/FAIL/NOT RUN/BLOCKED. Include actual
compiler C++20 and Debug /MTd / Release /MT evidence when applicable. Compare source
tag reference and previous approved phase; state deliberate output-root differences.
Preserve current tests and app startup/resource behavior; never claim results not run.

## Warning status and baseline-known failures

| Configuration | First-party compiler warnings | Linker warnings | Vendor warnings | Evidence |
| --- | --- | --- | --- | --- |

Zero first-party compiler and linker warnings are required where compilation occurs.
Document every known failure with baseline command/evidence, expected signature,
candidate result and why it is unchanged/outside Build scope. Mark missing reference
data unknown, not passing. Note runtime/resources/CWD and toolchain limitations.

## Human review checklist

- [ ] Diff fits this phase and is understandable as one checkpoint.
- [ ] File ownership, target closure and boundary changes are justified.
- [ ] Required validation and reference comparisons have recorded results.
- [ ] Warning/CRT/language gates and known-failure handling are accurate.
- [ ] DLL/resource origin, staging and CWD are covered when affected.
- [ ] Protected work remains unchanged and rollback is demonstrably isolated.
- [ ] Final snapshot matches the final candidate; no phase/commit/tag/push follows automatically.

## Final Validation Snapshot

Phase / timestamp:
Validated HEAD / branch / configured remote:
Source baseline/tag/peeled commit:
Previous approved tag/peeled commit:
Reference evidence identity:

| Exact relative path | Tracking classification | Entry existence | Operation | Raw SHA-256 or ABSENT | Expected Git blob | Checkpoint member |
| --- | --- | --- | --- | --- | --- | --- |

Attach the complete behavior/build-affecting input closure with hashes: changed
and unchanged source/headers, scripts, resources, packages/locks/profiles, generated
toolchain inputs, SDK/import libraries/DLLs and relevant environment/tool identity.
List exact phase-owned paths, including all new files and deletions. No glob stands
for a frozen inventory. Include validated command/results/log hashes above.

Literal `git status --short`:

```text
Paste captured output, or explicitly state empty.
```

Expanded untracked/ignored classification and exact index state:

Literal `git diff --check`, exit status, and new-file whitespace check results:

```text
Paste captured output, or explicitly state empty with exit status.
```

Protected unrelated files/index entries and entry/final hashes:
Known baseline failures and validation limits:
Context expansion log (specific additional section/source and dependency/blocker):

Seal location: `.codex/build/phase-XX-snapshot.json`. Finalize this review before
writing the seal, which hashes the review and complete candidate; record seal
SHA-256 in local BUILD_STATE. Do not put a recursive self-hash into this document.
List mutable local control exclusions (state, seal, transaction receipts) separately;
no behavior/build input qualifies for that exemption.
Any later behavior/build edit invalidates validation; any candidate review/document
edit invalidates the approval seal. Normal approval is cheap verification only.

## Approval and rejection boundary

Exact intended checkpoint files (only PHASE_OWNED_TRACKED / PHASE_OWNED_NEW_TRACKED):
Excluded LOCAL_GOVERNANCE / PROTECTED_UNRELATED:
Verified previous checkpoint and exact restore/remove actions if rejected:

PRE_APPROVAL_HEAD / POST_APPROVAL_HEAD, annotated tag object and push receipts are
recorded **later** in the local approval transaction, not invented during execution
or appended to the sealed review. No commit/tag/push has occurred during START/REVISE.
