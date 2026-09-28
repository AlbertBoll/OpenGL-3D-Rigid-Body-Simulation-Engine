# PRE_EDITOR proposal Revision 01

**PROPOSED / NOT_STARTED. Documentation only; no activation authorization.**

This owner-requested revision corrects three review findings in the existing proposal. It preserves the 00–55 phase numbering, milestone scope, dependencies, residual owners and technical OPEN statuses. Future OPEN_DECISION items remain due at their applicable entry or gate. Audit Completion remains COMPLETE_FOR_REVIEW, Plan Readiness READY_FOR_HUMAN_REVIEW, and Milestone A Readiness NOT_READY.

Revision entry was recorded at 2026-09-27T22:11:34.6033175+00:00. [The separate revision observation](../evidence/revision-01.json) retains entry/final checks, original inventory provenance, original observation hashes and the exact modified-file list. It is not a receipt, seal, workflow state or runtime validation result.

## 1. Phase 00 bootstrap

[C0](../VALIDATION_CONTRACTS.md#common-phase-contract-c0), [Phase 00](../phases/PHASE_00.md) and [the activation proposal](../ACTIVATION_PROPOSAL.md) now distinguish bootstrap from later entry. Phase 00 requires verified completed Rendering Phase 68, owner acceptance of the plan and separate explicit activation/minimal-setup authorization. It creates the isolated worktree/minimal workflow setup and stops for review. The already activated isolated-worktree requirement applies from Phase 01 onward.

This documentation revision supplies none of that future activation authorization.

## 2. Transform scaling and collider behavior

[HANDOFF_AUDIT S22](../HANDOFF_AUDIT.md#scale-mutation-and-collider-behavior-s22) now distinguishes:

- Changed Transform3DComponent::SetScale calls: emit the new-scale argument before storing Scale; unchanged values do not emit. A live connected runtime Physics shape receives the callback.
- Direct Scale writes/Transform construction: do not emit the signal. Initial shape construction has its own inputs: sphere fixture radius, box points with the initial Transform scale, convex points with default unit scale.
- Sphere callbacks: preserve the existing base-radius times X-component policy and finite-positive-result guard.
- Box/convex callbacks: rebuild from the stored source at an absolute component-wise scale, including supported signed/nonuniform values; invalid/degenerate rebuilds preserve the shape. The stored source can include startup scaling. Shape revisions feed existing body caches and sleep/wake change detection.
- Mesh regeneration/Bake and explicit collider edits: remain separate. Regeneration/Bake alone does not invoke the bridge; any associated Transform reset through SetScale must account for its existing collider effect. Explicit SetRadius/Build can establish a new collider base.

[Phase 08](../phases/PHASE_08.md) preserves supported behavior; [13](../phases/PHASE_13.md) migrates connection lifetime; [14](../phases/PHASE_14.md) deliberately separates the internal shape-update step from public notifications after successful authoritative mutation; [24](../phases/PHASE_24.md) integrates the reviewed collider/Transform contract. The findings ledger and provider table use the same distinction.

Authoritative read-only source anchors: Component/Component.h:209–225; _Scene.cpp:86–94,897–1084; Geometry.cpp:255–273; Physics/Shape.cpp:8–32, ShapeSphere.cpp:23–40, ShapeBox.cpp:61–90, ShapeConvex.cpp:596–621; PhysicsBody.cpp:164–256; PhysicsSystem.cpp:449–478. Existing PhysicsTests assertions at :660–786 were read, never executed. RBS constructor scale and legacy direct-field callers were inspected to distinguish mutation paths.

This clarification describes current semantics. It infers no new source defect and authorizes no implementation change.

## 3. Phase 52–53 admission timing

[P0](../VALIDATION_CONTRACTS.md#performance-admission-p0), [Phase 52](../phases/PHASE_52.md), [Phase 53](../phases/PHASE_53.md), OD-12 and the corresponding [original residual records](../PERFORMANCE_RESIDUALS.md) now share these gates:

1. Before START: approve bounded adapter/diagnostic scope, equivalence/lineage criteria, protocol, original applicable thresholds and execution limits.
2. During the phase: establish RBS adapter equivalence or historical source/build/input/metric lineage.
3. Before comparative performance measurement: verify and admit the required comparability for the exact workload and boundaries.
4. If comparability cannot be established: retain OPEN / INCONCLUSIVE and request explicit owner disposition at the declared stop. No weakened thresholds or indefinite retries.

Completed comparability evidence is therefore a gate before comparison, not a circular prerequisite for starting the diagnostic phase that produces it.

## Provenance and checks

The original baseline-observation.json, source-observation.json and final-preservation.json are preserved byte-for-byte, with their original timestamps. The master instruction is unchanged. The inventory keeps its original created_utc, records this revision separately and refreshes current file hashes/counts; its original contents/hash are retained in the revision observation.

Only focused documentation consistency, file/link/hash and protected-work/raw-index preservation checks are performed. No builds, tests, benchmarks, application runs, production/live-governance edits, Git staging/commit/tag/push, branch/worktree creation/switch or PRE_EDITOR activation.

The [updated plan](../PRE_EDITOR_PLAN.md), [compact manifest](../PROPOSAL_MANIFEST.json) and [current inventory](../evidence/package-files.json) are the review package. Stop for owner review; no subsequent phase starts.
