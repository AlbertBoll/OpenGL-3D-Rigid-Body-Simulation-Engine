# Physics Refactor and Optimization Plan

**Repository:** `GEngine-physics`  
**Planning baseline:** `PHYSICS_SYSTEM_AUDIT.md` (2026-09-03 audit)  
**Refactor starting point:** **Phase 06**  
**Status model:** `READY → IN PROGRESS → AWAITING HUMAN REVIEW → {APPROVED | FIX PENDING | REJECTED}`  
**Primary principle:** correctness and lifetime safety first, stability second, collision robustness third, performance fourth, architecture/parallelism last.

---

# Current Phase State and Numbering (2026-09-08)

This document is the authoritative phase numbering. The temporary Phase 31–33 amendment has been integrated into the main dependency graph and detailed phase definitions below; there is no second, conflicting set of Phase 32/33 objectives.

- **Phase 31 — Fixed Physics Timestep Accumulator: REJECTED.**
  - Its owned timing implementation was selectively reverted to the approved Phase 30 baseline.
  - `docs/physics/PHASE_31_REVIEW.md` is the permanent rejection/failure record.
  - No `physics-phase-31-approved` tag may be created or fabricated.
  - The architectural goal of fixed physics scheduling remains valid; only the Phase 31 implementation is rejected.
- **Phase 32 — Contact Solver Timestep Stability: current corrective phase.**
  - Baseline: `physics-phase-30-approved`.
  - Validate the exact exported 4×4 application world at fixed 60/120 Hz and 1/2/4/8 solver passes.
  - Correct only evidenced contact/solver defects required for robust resting behavior.
  - No sleeping, arbitrary damping, or application scheduling redesign.
- **Phase 33 — Fixed Physics Timestep Scheduling (Revisited).**
  - May begin only after explicit Phase 32 approval.
  - Re-implements the original Phase 31 architectural objective from the approved Phase 32 solver baseline.
- The original sleeping/island sequence is renumbered:
  - former Phase 32 → **Phase 34 — Sleeping State Primitives**
  - former Phase 33 → **Phase 35 — Contact Island Construction**
  - former Phase 34 → **Phase 36 — Sleep / Wake Integration**
- The original Phase 35–45 sequence is renumbered to **Phase 37–47**.

Phase 32 review: `docs/physics/PHASE_32_REVIEW.md`.

---
# 0. Purpose

This plan replaces all prior Phase 01–05 planning assumptions.

Phase 06 is the new logical starting point for physics work. No design decision, dependency, scope boundary, approval state, or implementation assumption from any earlier Phase 01–05 plan should be treated as authoritative.

The only technical planning baseline for this document is the current `PHYSICS_SYSTEM_AUDIT.md`.

The audit identifies:

- 3 Critical bugs
- 9 High-severity bugs
- 10 Medium-severity bugs
- 2 Low-severity bugs
- confirmed box-stack energy injection and collapse
- confirmed angular-energy growth for an asymmetric free rigid body
- sphere-lattice failure to settle
- EPA, manifold lookup, and solver algebra as the dominant measured collision-heavy performance costs
- no current evidence that SAP replacement should be a near-term priority

This plan therefore deliberately prevents optimization work from preceding correctness work.

---

# 1. Mandatory Phase Size Rules

Every phase MUST remain small enough to review manually.

## 1.1 Normal scope budget

A normal phase should target:

- **1 primary technical objective**
- **1 subsystem or tightly coupled boundary**
- **no more than 3 production source/header files where practical**
- **hard limit: 5 production files**
- **no more than 2 dedicated test files**
- **target total changed files: <= 7**
- no unrelated formatting, cleanup, renaming, or modernization

Build files and the phase review note may be changed only when strictly required.

If Codex determines that the phase requires more than 5 production files, it MUST stop and propose a smaller split before implementing the broader change.

## 1.2 Forbidden scope expansion

During any phase Codex MUST NOT:

- fix unrelated bugs discovered during implementation
- begin the next phase early
- combine correctness work with unrelated performance work
- change rendering, animation, audio, editor, or unrelated scene architecture
- perform repository-wide formatting
- rename broad APIs merely for style
- introduce multithreading before the dedicated parallelism phases
- replace SAP unless a later benchmark phase proves replacement is justified

Unexpected findings must be added to the phase review document as follow-up work.

---

# 2. Baseline Before Phase 06

Because this plan intentionally ignores prior Phase 01–05 state, Phase 06 must have an independent rollback baseline.

Before starting Phase 06:

1. Verify the current intended physics branch state.
2. Ensure unrelated local changes are either committed elsewhere, stashed, or explicitly preserved.
3. Run:
   ```bash
   git status --short
   git log -1 --oneline
   ```
4. Create an independent baseline tag on the exact commit from which Phase 06 begins:
   ```bash
   git tag physics-refactor-v2-baseline
   git push origin physics-refactor-v2-baseline
   ```
5. Record the baseline commit hash in the Phase 06 review notes.

Do not use an old Phase 05 tag as the logical baseline for this plan.

If the working tree contains required uncommitted engine changes, commit those intentionally before creating the baseline tag. Do not silently absorb unrelated user work.

---

# 3. Human Review State Machine

Each phase uses the following state machine.

```text
READY
  |
  v
IN PROGRESS
  |
  v
AWAITING HUMAN REVIEW
  |             |              |
  |             |              |
APPROVED    FIX PENDING      REJECTED
  |             |              |
  |             |              v
  |             |        ROLLED BACK
  |             |              |
  v             +------> same phase
next phase
```

## 3.1 AWAITING HUMAN REVIEW

When implementation and automated validation are complete, Codex MUST stop.

It must show:

- files changed
- implementation summary
- bugs addressed
- tests added/changed
- Debug test result
- Release test result
- benchmark result if the phase is performance-sensitive
- `git diff --check`
- `git status --short`
- known limitations
- exact items requiring human inspection

No commit, tag, push, or next phase is allowed yet.

---

# 4. APPROVED Workflow

If the human explicitly approves the phase:

1. Verify only phase-authorized files are changed.
2. Run the final required validation again.
3. Run:
   ```bash
   git diff --check
   git status --short
   ```
4. Commit with:
   ```text
   physics: phase XX <short objective>
   ```
5. Create:
   ```text
   physics-phase-XX-approved
   ```
6. Push the branch:
   ```bash
   git push origin <current-physics-branch>
   ```
7. Push the tag:
   ```bash
   git push origin physics-phase-XX-approved
   ```
8. Show:
   ```bash
   git log -1 --oneline
   git status --short
   git tag --list "physics-phase-XX-approved"
   ```
9. Only then may Phase XX+1 begin.

An approved tag is immutable. Do not move or recreate it.

---

# 5. FIX PENDING Workflow

Use **FIX PENDING** when the human does not reject the phase, but requires changes before approval.

Rules:

- remain on the same phase number
- do not rollback to the previous tag
- do not start the next phase
- do not create the approved tag
- do not push an "approved" commit
- make only corrections directly related to review feedback
- preserve the original phase objective and scope
- if review feedback would exceed the phase size limit, stop and propose a phase split

Recommended state text:

```text
PHASE XX STATUS: FIX PENDING
```

After fixes:

1. rerun all phase validation
2. update the phase review notes
3. return to:
   ```text
   AWAITING HUMAN REVIEW
   ```
4. stop again

There is no limit on review iterations, but scope must not expand.

---

# 6. REJECTED Workflow

Use **REJECTED** when the human decides the phase implementation should not continue.

The rollback baseline is the **most recent approved phase tag**, but rollback MUST be scoped by phase ownership.

Examples:

- Phase 06 rejected → baseline `physics-refactor-v2-baseline`
- Phase 12 rejected → baseline `physics-phase-11-approved`
- Phase 24 rejected → baseline `physics-phase-23-approved`
- Phase 31 rejected → baseline `physics-phase-30-approved`

Before rollback:

```bash
git status --short
git diff --name-only
git diff --cached --name-only
```

Then:

1. Identify the exact tracked files owned by the rejected phase.
2. Identify untracked files created by the rejected phase.
3. Explicitly separate unrelated/user-owned work.
4. If safe separation is not possible, **STOP and report the overlap** instead of overwriting unrelated work.
5. Restore only rejected-phase tracked files from the previous approved baseline, for example:
   ```bash
   git restore --source=<previous-approved-tag> --staged --worktree -- <phase-owned-file-1> <phase-owned-file-2> ...
   ```
6. Remove only untracked files proven to have been created by the rejected phase.
7. Preserve the phase review as a rejection/failure record when instructed by the human.

### Hard-reset rule

Do **not** use `git reset --hard` as the normal rejection mechanism.

It is allowed only when the human explicitly authorizes it **and** all working-tree/index changes are confirmed disposable. Never use blanket `git clean -fd` without explicit human authorization.

After rollback:

```bash
git status --short
git log -1 --oneline
```

The phase state becomes:

```text
PHASE XX STATUS: REJECTED / ROLLED BACK
```

No approved tag is created for a rejected phase.

If the human wants the rejection history persisted in Git, commit only the rejection review/documentation as an explicitly scoped audit/docs commit; do not create `physics-phase-XX-approved`.

A rejected objective may later be redesigned under the same phase number only when that number has not been reassigned. If the human assigns a new phase number, the rejected phase remains permanent history and the redesign uses the new number.
# 7. Validation Classes

Different phases require different validation.

## Class A — Lifetime / Memory Safety

Required:

- Debug build
- Release build
- physics tests
- dedicated destruction/restart regression
- `git diff --check`

Prefer when available:

- ASan / Application Verifier / checked iterator runtime

## Class B — Mathematical Correctness

Required:

- Debug build
- Release build
- focused mathematical regression tests
- full physics tests
- numerical tolerance documented
- no NaN/Inf

## Class C — Stability

Required:

- box-stack deterministic probe
- single-sphere settling probe where relevant
- sphere-lattice probe where relevant
- energy / max-speed / penetration summary
- Debug + Release physics tests

## Class D — Performance

Required:

- correctness suite first
- same benchmark configuration before/after
- warmup + multiple samples
- median result
- workload counters
- no regression in numerical output beyond approved tolerance
- measurements reported as measured, not estimated

---

# 8. Phase Dependency Overview

```text
Phase 06  Regression Baseline
   |
   +--> 07 Body Removal Safety
   |      |
   |      +--> 08 World Reset / Restart Safety
   |              |
   |              +--> 09 Stable Body Identity
   |                      |
   |                      +--> 10 Hide Mutable Body Storage
   |
   +--> 11 Convex Shape Validity
   +--> 12 Sphere/Base Shape Validity
   +--> 13 Angular Dynamics Correction
   +--> 14 Body Type / Mass Invariants
   |
   +--> 15 Canonical Contact Convention
          |
          +--> 16 Resting Coulomb Friction
          +--> 17 Ballistic Contact Guard + Friction
          +--> 18 Restitution Threshold
          +--> 19 Configurable Iterative Solver
          +--> 20 Non-Energy Position Stabilization
                  |
                  +--> 21 Box Contact Feature Extraction
                  +--> 22 Box Face Manifold Generation
                  +--> 23 Manifold Persistence Quality
   |
   +--> 24 Pure Conservative Advancement
          |
          +--> 25 TOI Revalidation
          +--> 26 Angular Swept Broad-Phase Bounds
   |
   +--> 27 GJK Robustness Contract
          |
          +--> 28 EPA Failure Telemetry / Robustness
   |
   +--> 29 Absolute Geometry Scaling Contract
          |
          +--> 30 Runtime Transform Synchronization
                  |
                  +--> 31 Fixed Physics Timestep Accumulator
                  |       REJECTED / historical only
                  |
                  +--> 32 Contact Solver Timestep Stability
                          |
                          +--> 33 Fixed Physics Timestep Scheduling (Revisited)
                                  |
                                  +--> 34 Sleeping State Primitives
                                          |
                                          +--> 35 Contact Island Construction
                                                  |
                                                  +--> 36 Sleep / Wake Integration
   |
   +--> 37 Fixed-Storage Solver Math Optimization
   +--> 38 O(1)-Average Manifold Pair Index
   +--> 39 EPA Scratch Allocation Optimization
   +--> 40 EPA Topology Optimization
   +--> 41 Local-Space Support Optimization
   +--> 42 Unified GJK Distance / Contact Query
   +--> 43 Sphere-Box Specialized Narrow Phase
   +--> 44 Box-Box Specialized Narrow Phase
   +--> 45 SAP Pathological-Scene Optimization
   +--> 46 Final Physics Performance Consolidation
   |
   +--> 47 Optional Parallel Narrow Phase / Islands
```

Phase 31 is a rejected historical node, not a prerequisite tag. The active timing dependency is:

```text
physics-phase-30-approved
    -> Phase 32 approval
    -> physics-phase-32-approved
    -> Phase 33
```

Parallelism remains intentionally last.
# 9. Detailed Phase Plan

---

## Phase 06 — Physics Regression Baseline

### Goal

Create a deterministic regression harness that captures the current audited failure modes before modifying engine behavior.

### Primary findings covered

Audit measurement infrastructure and future regression needs.

### Allowed files

Prefer tests/probe infrastructure only:

- `PhysicsTests/src/main.cpp`
- physics benchmark/probe source if already present
- at most one small test helper file

Production physics code should remain untouched.

### Add regression coverage for

- body deletion while manifold exists
- runtime physics stop/start
- empty/degenerate convex shape
- asymmetric free-body angular energy
- A/B contact order permutation
- 4×4 box stack stability metrics
- single-sphere settling
- sphere-lattice settling metrics

Tests for currently broken behavior may initially be marked as expected failure or diagnostic checks, but the harness must not hide crashes.

### Exit criteria

- deterministic reproduction commands documented
- current baseline measurements recorded
- no production physics behavior change
- Class B/C validation applicable

### Review focus

Confirm the harness represents the actual failures and does not overfit future implementation.

---

## Phase 07 — Safe Body Removal / Manifold Invalidation

### Goal

Fix dangling manifold references when a rigid body is removed.

### Primary finding

`PHYS-BUG-001`

### Target files

Expected:

- `PhysicsWorld.cpp`
- `Manifold.cpp` / `Manifold.h`
- `PhysicsSystem.cpp` only if notification routing is required
- focused test file

### Scope

Implement one explicit invalidation path so contacts/manifolds referencing a body are removed before that body's memory is released.

### Do not include

- full RAII conversion
- stable body handles
- world restart redesign
- manifold hash optimization

### Exit criteria

- remove body with active manifold does not crash
- no stale contact remains
- full physics tests pass

### Validation

Class A.

---

## Phase 08 — Physics World Reset and Restart Safety

### Goal

Make `OnExit()` / world replacement / runtime restart idempotent and safe.

### Primary findings

`PHYS-BUG-002`  
part of `PHYS-BUG-021`

### Target files

Expected:

- `PhysicsSystem.cpp`
- `PhysicsSystem.h`
- `_Scene.cpp` only if runtime stop/start call path must be corrected
- focused test

### Scope

- clear manifolds before world destruction
- clear transient pair/contact state
- prevent leaking the prior world
- make repeated stop/start safe

### Do not include

- shape ownership conversion
- body-handle redesign

### Exit criteria

Repeated:

```text
start -> collide -> stop -> start -> collide -> stop
```

must pass without stale state.

### Validation

Class A.

---

## Phase 09 — Stable Internal Body Identity

### Goal

Introduce a stable internal body identity/generation mechanism needed for safe cached-pair references.

### Findings enabled

`PHYS-API-004`  
future `PHYS-PERF-002`

### Target files

Expected:

- `PhysicsBody.h`
- `PhysicsWorld.h`
- `PhysicsWorld.cpp`
- one focused test

### Scope

Identity only.

Do not convert all raw pointers in this phase.

### Exit criteria

- each live body has stable unique identity
- reused/deleted body memory cannot impersonate an older body generation
- identity creation/removal tests pass

---

## Phase 10 — Hide Mutable Physics Body Storage

### Goal

Prevent external mutation of the world's owning body vector.

### Primary findings

`PHYS-BUG-019`  
`PHYS-API-002`

### Target files

Expected:

- `PhysicsWorld.h`
- `PhysicsWorld.cpp`
- direct call sites only if compile-required
- tests

### Scope

Replace public mutable vector access with a bounded read-only iteration/view API and validated creation/removal calls.

### Hard scope rule

If more than 5 production call-site files require migration, stop and split Phase 10 into 10A/10B before continuing.

### Exit criteria

Client code cannot insert null/stale pointers into the owning container.

---

## Phase 11 — Convex Shape Validity Contract

### Goal

Make invalid convex geometry fail safely and transactionally.

### Primary finding

`PHYS-BUG-003`

### Target files

Expected:

- `ShapeConvex.h`
- `ShapeConvex.cpp`
- focused tests

### Cases

- zero points
- fewer than four usable points
- coplanar points
- non-finite points
- zero-volume hull
- invalid support request

### Exit criteria

No invalid construction path reaches empty-vector indexing or divide-by-zero.

### Validation

Class A/B.

---

## Phase 12 — Sphere and Base Shape Validity

### Goal

Correct default shape initialization and sphere radius validity.

### Primary finding

`PHYS-BUG-020`

### Target files

Expected:

- `Shape.h` / `Shape.cpp`
- `ShapeSphere.h` / `ShapeSphere.cpp`
- focused tests

### Scope

- deterministic base initialization
- finite positive radius
- transactional radius mutation
- valid revision behavior

### Do not include

scene scaling semantics; that is Phase 29.

---

## Phase 13 — Correct Torque-Free Angular Dynamics

### Goal

Correct the gyroscopic sign error and use the already cached world inverse inertia.

### Primary finding

`PHYS-BUG-004`  
`PHYS-PERF-011`

### Target files

Expected:

- `PhysicsBody.cpp`
- focused rigid-body math test

### Scope

Only angular update mathematics and directly redundant inverse use.

### Exit criteria

Free asymmetric-body test must satisfy an explicitly approved angular-energy / angular-momentum tolerance over 10 seconds.

### Validation

Class B.

No solver tuning is allowed here.

---

## Phase 14 — Enforce Body Type / Mass Invariants

### Goal

Make Static, Dynamic, and Kinematic semantics internally consistent.

### Primary finding

`PHYS-BUG-013`

### Target files

Expected:

- `PhysicsBody.h`
- `PhysicsBody.cpp`
- `PhysicsSystem.cpp`
- focused tests

### Define and enforce

- static: zero inverse mass, no integration from velocity/forces, no dynamic impulse response
- dynamic: positive mass behavior
- kinematic: externally driven, no gravity/impulse response unless explicitly designed otherwise

### Exit criteria

No contradictory Type/inverse-mass state can silently enter the solver.

---

## Phase 15 — Canonical Contact A/B/Normal Convention

### Goal

Define and enforce one contact orientation convention across collision, manifolds, and constraints.

### Primary finding

`PHYS-BUG-006`

### Target files

Expected:

- `Contact.h`
- `Manifold.cpp`
- `ConstraintPenetration.cpp` only if sign alignment requires it
- focused permutation tests

### Contract

The plan must state exactly whether the normal points:

```text
A -> B
```

or:

```text
B -> A
```

and every collision/solver path must follow it.

### Exit criteria

Swapping body order yields physically equivalent impulses with appropriately inverted normal.

### Validation

Class B.

---

## Phase 16 — Resting Coulomb Friction

### Goal

Replace the resting-contact friction clamp with accumulated Coulomb friction.

### Primary finding

`PHYS-BUG-007`

### Target files

Expected:

- `ConstraintPenetration.cpp`
- `ConstraintPenetration.h` only if state changes
- focused solver tests

### Scope

- accumulated normal lambda
- 2D tangent impulse projection
- `||lambda_t|| <= mu * lambda_n`
- remove gravity-like hard-coded friction floor

### Do not include

ballistic friction; that is Phase 17.

### Validation

Class B/C focused sliding/sticking cases.

---

## Phase 17 — Ballistic Separating Guard and Coulomb Friction

### Goal

Make positive-TOI impulse response reject separating contacts and clamp tangential impulse by normal impulse.

### Primary findings

part of `PHYS-BUG-010`  
`PHYS-BUG-022`

### Target files

Expected:

- `PhysicsSystem.cpp`
- focused collision-response tests

### Scope

No TOI event-list redesign yet.

### Exit criteria

- no attractive normal impulse when currently separating
- grazing impact friction obeys Coulomb bound

---

## Phase 18 — Restitution Velocity Threshold

### Goal

Prevent low-speed resting contacts from repeatedly receiving ballistic restitution.

### Primary finding

`PHYS-BUG-015`

### Target files

Expected:

- `PhysicsSystem.cpp`
- possibly one physics configuration header
- tests

### Scope

Introduce a documented threshold and material combine behavior.

### Exit criteria

Low-height bouncing sphere settles under a defined criterion without removing intended high-speed bounce.

---

## Phase 19 — Configurable Iterative Solver Passes

### Goal

Replace the single hard-coded global solve pass with a bounded configurable iteration count.

### Primary finding

part of `PHYS-BUG-005`

### Target files

Expected:

- `PhysicsSystem.cpp`
- `PhysicsSystem.h` or physics config file
- tests/benchmark

### Scope

Only repeated solver traversal.

Do not introduce islands yet.

### Exit criteria

- iteration count configurable
- deterministic
- stack metric improves or stays neutral
- performance cost reported per iteration count

### Validation

Class C/D.

---

## Phase 20 — Non-Energy-Injecting Position Stabilization

### Goal

Separate penetration stabilization from physical velocity response.

### Primary finding

`PHYS-BUG-008`

### Target files

Expected:

- `ConstraintPenetration.cpp`
- `ConstraintPenetration.h`
- at most one integration/solver orchestration file
- tests

### Scope

Implement one approved approach:

- split impulse, or
- separate bounded positional correction

Do not redesign contacts or add box clipping here.

### Exit criteria

Box-stack penetration improves without mechanical energy exceeding the approved bound.

### Validation

Class C.

---

## Phase 21 — Box Contact Feature Extraction Foundation

### Goal

Identify stable box contact features needed for real face manifolds.

### Primary finding

part of `PHYS-BUG-005`

### Target files

Expected:

- `ShapeBox.cpp`
- `ShapeBox.h`
- new small box-contact helper if necessary
- tests

### Scope

Feature extraction only:

- candidate face
- incident/reference feature
- local feature identifier
- stable normal-compatible selection

Do not feed multiple contacts into solver yet.

---

## Phase 22 — Box Face Manifold Generation

### Goal

Generate 2–4 geometrically meaningful contact points for box face contacts.

### Primary finding

`PHYS-BUG-005`

### Target files

Expected:

- box-contact helper
- contact/manifold integration file
- focused tests

### Scope

- reference/incident clipping
- bounded contact reduction
- consistent penetration/normal convention

### Hard limit

Do not simultaneously replace the entire box-box collision algorithm unless required. If SAT replacement becomes necessary, stop and split the phase.

### Exit criteria

Resting aligned and rotated face contacts produce stable 2–4 point manifolds.

---

## Phase 23 — Manifold Persistence Quality

### Goal

Persist contacts using stable feature/pair identity and normal coherence.

### Primary weaknesses

manifold duplicate logic, stale warm-start risk, no feature identity

### Target files

Expected:

- `Manifold.h`
- `Manifold.cpp`
- `Contact.h` if feature key storage is required
- tests

### Scope

- pairwise anchor/feature comparison
- normal coherence
- reset warm-start lambda when feature/normal validity fails
- shape/body generation compatibility

### Exit criteria

Stable box face contacts retain useful impulses without preserving invalid constraints.

---

## Phase 24 — Pure Conservative Advancement Queries

### Goal

Remove temporary mutation/rewind of live rigid-body state during collision prediction.

### Primary finding

`PHYS-BUG-009`

### Target files

Expected:

- `PhysicsSystem.cpp`
- one prediction helper if needed
- tests

### Scope

Compute predicted transforms locally.

Do not redesign event ordering yet.

### Exit criteria

Repeated collision queries do not modify live body pose/velocity/cache state.

---

## Phase 25 — TOI Revalidation After Earlier Impulses

### Goal

Prevent stale simultaneous TOI contacts from applying obsolete impulses.

### Primary finding

remaining `PHYS-BUG-010`

### Target files

Expected:

- `PhysicsSystem.cpp`
- focused simultaneous-impact tests

### Scope

Implement the smallest correct strategy:

- revalidate current contact state before impulse
- recompute when required
- discard stale/separating events

Do not introduce a full speculative-contact engine in this phase.

---

## Phase 26 — Angular Swept Broad-Phase Bounds

### Goal

Make broad-phase swept bounds conservative for rotating bodies.

### Primary finding

`PHYS-BUG-011`

### Target files

Expected:

- `Broadphase.cpp`
- `ShapeBox.cpp`
- `ShapeConvex.cpp`
- tests

### Scope

- angular extent expansion or conservative rotational start/end bound
- fix local/world frame mismatch in angular speed estimate

### Exit criteria

Fast rotating long-box regression no longer tunnels due to broad-phase rejection.

---

## Phase 27 — GJK Robustness Contract

### Goal

Bound GJK execution and make tolerances scale-aware enough for supported engine scales.

### Primary finding

`PHYS-BUG-017`

### Target files

Expected:

- `GJK.cpp`
- `GJK.h` only if result status expands
- tests

### Scope

- explicit iteration cap
- typed termination reason or counters
- scale-aware progress tolerance
- preserve existing valid contact outputs within tolerance

### Exit criteria

Scale/fuzz regression suite has no non-terminating path and failures are observable.

---

## Phase 28 — EPA Failure Telemetry and Robustness

### Goal

Make EPA failure modes explicit and testable before performance optimization.

### Primary finding

`PHYS-BUG-018`

### Target files

Expected:

- `GJK.cpp` / EPA implementation
- profiling/result header if needed
- focused tests

### Scope

- typed failure reason
- iteration/failure counter
- scale-aware duplicate/convergence tolerance where justified
- no topology optimization yet

### Exit criteria

Failure is distinguishable from separation and no unsafe contact escapes validation.

---

## Phase 29 — Absolute Geometry Scaling Contract

### Goal

Fix geometry scaling so runtime scale is absolute rather than multiplicatively compounded.

### Primary finding

part of `PHYS-BUG-012`

### Target files

Expected:

- `Shape.cpp`
- `ShapeSphere.cpp` / header
- `ShapeBox.cpp` only if required
- focused tests

### Scope

- immutable/base geometry source
- absolute rebuild semantics
- revision increment
- radius and box scale sequence tests

### Exit criteria

Scale sequence 1 → 2 → 3 results in exactly 3× base geometry, not 6×.

---

## Phase 30 — Runtime Transform Synchronization Contract

### Goal

Define and implement authoritative runtime synchronization for physics pose edits.

### Primary finding

remaining `PHYS-BUG-012`

### Target files

Expected:

- `_Scene.cpp`
- transform/physics bridge component
- one physics API file
- tests

### Scope

Separate static/kinematic/dynamic authority rules.

### Hard rule

If this requires broad Scene/ECS redesign, stop and split the phase.

---

## Phase 31 — Fixed Physics Timestep Accumulator — REJECTED

### Status

```text
REJECTED / OWNED IMPLEMENTATION ROLLED BACK
```

### Historical goal

Make physics stepping deterministic with bounded fixed-step catch-up.

### Primary finding

`PHYS-BUG-014`

### Outcome

The Phase 31 implementation is rejected. Its owned timing changes were selectively reverted to `physics-phase-30-approved`, while unrelated user/application work was preserved.

The rejection does **not** reject fixed timestep as an architectural direction. It records that scheduling must not be approved until resting-contact solver stability is independently established.

### Permanent record

- Preserve `docs/physics/PHASE_31_REVIEW.md`.
- Do not create `physics-phase-31-approved`.
- Do not restore the rejected implementation as the starting point for later timing work.
- The redesigned fixed-scheduling objective is Phase 33 and must start from `physics-phase-32-approved`.

### Start rule

Phase 31 is historical and **must not be started again** under the current numbering.

---

## Phase 32 — Contact Solver Timestep Stability

### Goal

Establish robust resting-contact behavior at fixed 60 Hz and 120 Hz before application scheduling is changed again.

### Primary findings

Current follow-up to:

- `PHYS-BUG-005` — resting-contact / box-stack instability
- `PHYS-BUG-007` — Coulomb friction correctness
- `PHYS-BUG-008` — non-energy-injecting stabilization interaction

`PHYS-BUG-014` scheduling is explicitly deferred to Phase 33.

### Baseline

`physics-phase-30-approved`

A Phase 31 approved tag is not a prerequisite and must not be fabricated.

### Target files

Normal scope should remain at the contact/manifold boundary plus focused tests and phase documentation.

Expected production ownership:

- `ConstraintPenetration.cpp`
- `ConstraintPenetration.h`
- `Manifold.cpp`

If more than 5 production files are required, stop and propose a split.

### Required investigation matrix

Use the exact exported 4×4 application world, not a reconstructed stack.

Run:

- fixed 60 Hz and 120 Hz
- 1, 2, 4, and 8 global solver passes
- a primary post-settling window
- a late window capable of detecting delayed bursts

Measure:

- finite state
- peak linear/angular motion
- penetration
- excursion
- average stack height
- total mechanical energy
- manifold/contact persistence
- solver/contact convergence
- solver cost

### Solver/contact inspection scope

Inspect only evidenced causes, including as needed:

- accumulated impulse projection
- normal complementarity
- warm starting
- normal/tangent coupling
- anisotropic tangent effective mass
- multi-point manifold support coupling
- contact persistence

### Forbidden work

Do not add:

- sleeping
- arbitrary damping
- application fixed-timestep scheduling
- unrelated broad-phase or collision optimizations
- speculative solver rewrites not justified by red evidence

### Exit criteria

- exact exported world passes the approved stability gates at both rates and all requested pass counts
- retained default pass count is evidence-based
- focused contact-convergence regressions pass
- full Debug and Release physics suites pass
- no NaN/Inf
- no unreported mechanical-energy injection
- performance impact is measured and reported
- application scheduling remains unchanged

### Validation

Class B/C/D where applicable.

### Review

`docs/physics/PHASE_32_REVIEW.md`

Human approval is required before Phase 33 may start.

---

## Phase 33 — Fixed Physics Timestep Scheduling (Revisited)

### Goal

Re-implement deterministic fixed physics scheduling from the approved Phase 32 solver baseline, with bounded catch-up and explicit timing ownership.

### Primary finding

`PHYS-BUG-014`

### Dependency

Required predecessor:

```text
physics-phase-32-approved
```

Do not use the rejected Phase 31 implementation as the code baseline.

### Target files

Expected:

- application/simulation loop timing owner
- `RigidBodySimulation.cpp` only if it is still part of the approved stepping boundary
- one timing/config header if strictly required
- focused scheduling tests/probes

Respect the normal production-file scope limit.

### Scope

- one authoritative fixed physics `dt`
- elapsed-real-time accumulator
- bounded catch-up policy
- clear ownership of where physics steps occur
- no physics time advancement tied directly to render-frame count
- no arbitrary loss of elapsed time without a documented overload policy
- preserve unrelated scene/application setup work
- render interpolation may be deferred if it expands scope

### Required correctness properties

A render frame must not simply perform a hard-coded number of physics steps.

The schedule must behave conceptually as:

```text
accumulator += elapsedFrameTime

while accumulator >= fixedDt and catch-up budget remains:
    PhysicsStep(fixedDt)
    accumulator -= fixedDt
```

The exact overload policy must be documented and tested.

### Required validation

Compare equivalent simulated time under multiple render-frame timing patterns, including:

- steady frame timing
- faster-than-physics rendering
- slower-than-physics rendering
- bounded frame-time jitter
- a controlled long-frame/catch-up case

Re-run application regressions that exposed Phase 31 failure:

- 4×4 box stack
- sphere lattice
- relevant performance/FPS or whole-step measurements

### Hard separation rule

Phase 33 must not modify Phase 32 contact-solver equations merely to make scheduling pass.

If fixed scheduling reveals a new solver defect, stop and report evidence rather than silently combining solver and scheduling changes.

### Exit criteria

- equivalent physics results under varied render-frame timing within approved tolerance
- no duplicate or skipped physics stepping in normal operation
- bounded catch-up behavior is deterministic and documented
- box-stack stability from Phase 32 is preserved
- sphere-lattice behavior/performance does not reproduce the rejected Phase 31 regression
- Debug and Release physics tests pass
- no unrelated scene/render behavior change

### Validation

Class C/D as applicable.

---

## Phase 34 — Sleeping State Primitives

### Goal

Add body-level sleep data and thresholds without yet building full islands.

### Primary performance/stability gap

No sleeping implementation.

### Target files

Expected:

- `PhysicsBody.h`
- `PhysicsBody.cpp`
- `PhysicsSystem.cpp`
- tests

### Scope

- sleeping flag
- linear/angular threshold
- inactivity timer
- explicit wake method

Do not optimize broad phase or solver yet.

---

---

## Phase 35 — Contact Island Construction

### Goal

Build deterministic contact islands for solver/sleep boundaries.

### Target files

Expected:

- new small island helper
- `PhysicsSystem.cpp`
- manifold/body identity access
- tests

### Scope

Graph construction only.

No parallel execution.

### Exit criteria

Island membership is deterministic and matches contact connectivity.

---

---

## Phase 36 — Sleep / Wake Integration

### Goal

Use islands to safely sleep settled groups and wake them on relevant interaction.

### Primary performance finding

`PHYS-PERF-005`

### Target files

Expected:

- `PhysicsSystem.cpp`
- body sleep implementation
- island helper
- tests

### Required wake sources

- contact with active dynamic body
- explicit body mutation
- velocity/impulse change
- kinematic interaction

### Exit criteria

Sphere lattice and stable box stack settle to mostly sleeping bodies without missed wake-up.

### Validation

Class C/D.

---
# 10. Performance Optimization Phases

Correctness and stability phases above must be approved before these begin.

---

## Phase 37 — Fixed-Storage Solver Math

### Goal

Remove heap-backed fixed-size `Vec` / `Mat` overhead from the contact solver.

### Primary finding

`PHYS-PERF-003`

### Target files

Expected:

- `ConstraintPenetration.cpp`
- physics-specific small math helper
- possibly `ConstraintPenetration.h`
- tests/benchmark

### Scope

Do not rewrite the general engine math library unless unavoidable.

Prefer physics-local fixed arrays or scalarized effective-mass math.

### Required benchmark

100 / 1,000 / 10,000 collision-heavy workload and sphere lattice.

---

## Phase 38 — O(1)-Average Manifold Pair Index

### Goal

Replace linear manifold pair lookup with stable-key indexing.

### Primary finding

`PHYS-PERF-002`

### Dependency

Requires approved stable body identity and safe destruction.

### Target files

Expected:

- `Manifold.h`
- `Manifold.cpp`
- possibly one stable-pair-key helper
- tests/benchmark

### Exit criteria

Pair insertion/find behavior remains identical while 10k isolated-contact manifold cost materially decreases.

---

## Phase 39 — EPA Reusable Scratch Storage

### Goal

Remove repeated EPA vector allocation/copy pressure without changing topology algorithm.

### Primary finding

part of `PHYS-PERF-001`

### Target files

Expected:

- EPA/GJK implementation
- one scratch helper if needed
- benchmark tests

### Scope

Capacity reuse only.

No adjacency redesign yet.

### Exit criteria

Identical EPA result set within approved tolerance and reduced EPA time/allocation pressure.

---

## Phase 40 — EPA Horizon / Topology Optimization

### Goal

Reduce repeated O(F²) edge/topology work.

### Primary finding

remaining `PHYS-PERF-001`

### Target files

Expected:

- EPA/GJK implementation
- topology helper
- tests/benchmark

### Scope

Introduce explicit adjacency/horizon representation while retaining fail-safe validation.

### Required acceptance

No reduction in correctness coverage or failure observability from Phase 28.

---

## Phase 41 — Local-Space Support Mapping Optimization

### Goal

Avoid transforming every box/convex vertex to world space per support query.

### Primary finding

`PHYS-PERF-007`

### Target files

Expected:

- `ShapeBox.cpp`
- `ShapeConvex.cpp`
- tests/benchmark

### Scope

- transform search direction once into local space
- select local extreme
- transform only selected point back to world

### Exit criteria

Support outputs match existing implementation within tolerance.

---

## Phase 42 — Unified GJK Distance / Contact Query

### Goal

Avoid duplicate GJK searches for separated conservative-advance queries.

### Primary finding

`PHYS-PERF-006`

### Target files

Expected:

- `GJK.cpp`
- `GJK.h`
- `PhysicsSystem.cpp`
- tests/benchmark

### Scope

Return separation/contact status and witnesses from one bounded simplex search.

### Dependency

Phase 27 GJK robustness contract must already be approved.

---

## Phase 43 — Specialized Sphere-Box Narrow Phase

### Goal

Avoid generic GJK/EPA for sphere-box pairs while preserving the common contact contract.

### Primary finding

part of `PHYS-PERF-004`

### Target files

Expected:

- narrow-phase dispatch implementation
- new sphere-box helper
- tests/benchmark

### Exit criteria

Cross-check against generic path over randomized valid cases.

Do not add box-box specialization here.

---

## Phase 44 — Specialized Box-Box Narrow Phase

### Goal

Move box-box contacts to a dedicated robust path compatible with Phase 22 manifolds.

### Primary finding

remaining `PHYS-PERF-004`

### Target files

Expected:

- box-box contact helper
- narrow-phase dispatch
- tests/benchmark

### Hard rule

If this requires more than 5 production files, split into:

```text
Phase 44A — box-box separation/SAT
Phase 44B — manifold/dispatch integration
```

before implementation.

---

## Phase 45 — SAP Pathological-Scene Optimization

### Goal

Optimize the existing SAP only where measured overlap-heavy scenes prove a bottleneck.

### Primary finding

`PHYS-PERF-010`

### Target files

Expected:

- `Broadphase.cpp`
- `Broadphase.h`
- benchmark/test

### Candidate bounded changes

- O(1) active-set removal
- axis-selection heuristic
- separate static/dynamic handling

### Explicit non-goal

Do not replace SAP with BVH/KD-tree/octree unless benchmark evidence proves the current algorithm is no longer appropriate.

---

## Phase 46 — Final Physics Performance Consolidation

### Goal

Measure the cumulative result of approved correctness and performance phases.

### Production files

Ideally none.

### Required workloads

- 100 separated bodies
- 1,000 separated bodies
- 10,000 separated bodies
- 100 collision-heavy bodies
- 1,000 collision-heavy bodies
- 10,000 collision-heavy bodies
- 4×4 box stack
- single sphere
- 180-sphere lattice
- rotating CCD case

### Required outputs

For baseline vs current:

- whole step median
- broad phase
- narrow phase
- GJK calls/iterations
- EPA calls/iterations/failures
- manifold lookup
- solver
- sleeping body count
- contacts/manifold points
- energy/stability metrics

### Deliverable

Update the physics benchmark/report section only.

No speculative optimization is allowed in this phase.

---

# 11. Optional Architecture / Parallelism Phase

---

## Phase 47 — Optional Parallel Narrow Phase and Independent Islands

### Goal

Introduce parallel work only after ownership, profiler state, islands, scratch storage, and deterministic outputs are stable.

### Preconditions

All must be approved:

- stable body identity/lifetime
- deterministic pair/contact output
- island construction
- per-thread or reducible profiling
- per-worker scratch storage
- deterministic result merge policy
- stable single-thread baseline

### Scope

Begin with **one** of:

1. parallel narrow-phase pairs, or
2. parallel independent-island solving

Do not implement both in one phase.

If both are desired, split into:

```text
Phase 47A — parallel narrow phase
Phase 47B — parallel independent islands
```

### Required validation

- deterministic or explicitly bounded result differences
- race detector / concurrency checks where available
- 1-thread equivalence mode
- 2/4/8 worker benchmark
- no regression in box/lattice stability

---

# 12. Phase Review File

Every phase should maintain one small review document, for example:

```text
PHASE_06_REVIEW.md
PHASE_07_REVIEW.md
...
```

Each review file should contain:

```markdown
# Phase XX Review

## Status
AWAITING HUMAN REVIEW

## Objective

## Audit Findings Addressed

## Allowed Scope

## Files Changed

## Implementation Summary

## Tests Added

## Validation Commands

## Debug Result

## Release Result

## Benchmark / Stability Result

## Behavior Changes

## Known Limitations

## Out-of-Scope Findings

## Git Diff Check

## Git Status

## Human Decision
PENDING
```

The review file is phase-scoped and may be committed with the phase when approved.

---

# 13. Codex Instructions at the Start of Every Phase

Use this pattern:

```text
BEGIN PHASE XX.

Read:
- PHYSICS_SYSTEM_AUDIT.md
- PHYSICS_REFACTOR_OPTIMIZATION_PLAN.md
- PHASE_XX_REVIEW.md if it already exists

Work only on Phase XX.

Do not start any later phase.
Do not fix unrelated findings.
Respect the production-file scope limit.
If the required implementation exceeds the phase limit, STOP and propose a split.

Before changing code:
1. show current git status
2. identify the previous approved tag
3. list the exact files expected to change
4. state the Phase XX acceptance tests

Implement the minimum change required for this phase.

Run all required validation.

Create/update PHASE_XX_REVIEW.md.

Set:
PHASE XX STATUS: AWAITING HUMAN REVIEW

Then STOP.

Do not commit.
Do not tag.
Do not push.
Do not begin Phase XX+1.
```

---

# 14. Codex Instructions After Human Approval

Use:

```text
APPROVE PHASE XX.

The reviewed Phase XX implementation is approved.

Verify that only Phase XX authorized files are changed.

Rerun the required final validation and git diff --check.

Commit with:

physics: phase XX <approved objective>

Create the immutable tag:

physics-phase-XX-approved

Push the current physics branch to origin.

Push the approved tag to origin.

Then show:
- git log -1 --oneline
- git status --short
- git tag --list "physics-phase-XX-approved"

Do not begin Phase XX+1.

Then stop.
```

---

# 15. Codex Instructions for FIX PENDING

Use:

```text
PHASE XX IS FIX PENDING.

The phase is neither approved nor rejected.

Do not rollback.
Do not start Phase XX+1.
Do not create or push an approved tag.

Address only the following human-review findings:

<insert review findings>

Keep all revisions inside the original Phase XX objective and file-scope budget.

If the requested fixes require broader architectural work or more than the allowed file scope, STOP and propose a smaller split instead of expanding the phase.

Rerun all Phase XX validation.

Update PHASE_XX_REVIEW.md.

Return the phase to:

AWAITING HUMAN REVIEW

Then stop.
```

---

# 16. Codex Instructions for Rejection

Use:

```text
REJECT PHASE XX.

The Phase XX implementation is rejected.

Do not preserve rejected implementation changes, but preserve unrelated/user-owned work.

First show:
- git status --short
- git diff --name-only
- git diff --cached --name-only

Identify:
1. the previous approved baseline tag
2. the exact tracked files owned by Phase XX
3. the exact untracked files created by Phase XX
4. any unrelated/user-owned changes that must be preserved

If ownership cannot be separated safely, STOP and report the overlap.

Restore only the Phase XX tracked files from the previous approved tag using a path-scoped restore, for example:

git restore --source=<previous-approved-tag> --staged --worktree -- <phase-owned-files>

Remove only untracked files proven to have been created by Phase XX.

Do not use git reset --hard or git clean -fd unless the human explicitly authorizes discarding the affected work.

Preserve/update PHASE_XX_REVIEW.md as the rejection record when instructed.

Then show:
- git log -1 --oneline
- git status --short
- current HEAD / nearest approved tag

Mark:

PHASE XX STATUS: REJECTED / ROLLED BACK

Do not create physics-phase-XX-approved.
Do not restart the phase.
Do not begin the next phase.

Then stop.
```
# 17. Global Acceptance Gates

No phase is approvable if any of the following is true:

- Debug physics tests fail
- Release physics tests fail
- new NaN/Inf is introduced
- a memory-safety regression is observed
- `git diff --check` fails
- unrelated files are modified
- the phase silently changes behavior outside its objective
- required baseline/after measurements are missing for a performance phase
- Codex cannot explain why each changed production file is necessary
- the phase exceeds the file-scope limit without prior human-approved split

---

# 18. Physics Stability Gates

After Phase 20 and again after Phase 23, the following workloads must be explicitly reviewed:

## Box stack

Track:

- peak total mechanical energy relative to initial
- final average stack height
- max linear speed
- max angular speed
- final residual linear/angular motion
- contact/manifold count

The audited baseline showed approximately:

```text
peak total energy: 105.17% of initial
max linear speed: 10.42
partial stack collapse
```

The goal is not merely to suppress visible jitter. The solver must improve physical behavior.

## Sphere lattice

Track:

- final moving-body count
- max linear speed after 10 seconds
- max angular speed
- sleeping body count after sleeping is introduced
- solver time

The audited baseline retained high motion after 10 seconds and spent most of the step in the solver.

---

# 19. Performance Gates

Performance optimization phases must use the same benchmark shape and workload configuration before/after.

The audit baseline identified the dominant 10,000-body collision-heavy costs as approximately:

```text
whole step:       ~455.8 ms
narrow phase:     ~200.6 ms
EPA:              ~188.2 ms
manifold work:    ~147.0 ms
solver:            ~99.4 ms
broad phase:        ~4.2 ms
```

Therefore:

- do not optimize SAP first
- do not claim broad-phase replacement is necessary without new evidence
- do not optimize solver storage before solver equations are approved
- do not optimize manifold lookup before lifetime identity is safe
- do not optimize EPA topology before EPA failure/output behavior is covered

---

# 20. Final Intended Architecture Direction

This plan does **not** require a wholesale engine rewrite.

The preferred evolution is incremental:

```text
Unsafe raw lifetime
    ->
stable destruction + body identity
    ->
validated geometry
    ->
correct rigid-body dynamics
    ->
canonical contacts
    ->
correct solver/friction/stabilization
    ->
stable box manifolds
    ->
pure/revalidated CCD
    ->
robust GJK/EPA contracts
    ->
fixed timestep
    ->
sleeping/islands
    ->
measured allocation/data-structure optimization
    ->
specialized narrow phase
    ->
only then optional parallelism
```

Major technologies such as:

- full ECS/SoA physics rewrite
- job-system conversion
- BVH/KD-tree/octree replacement
- GPU physics
- wholesale solver replacement

are explicitly outside the current plan unless later measurements justify a new separately reviewed plan.

---

# 21. Summary of Phase Groups

| Group | Phases | Objective |
|---|---|---|
| Baseline | 06 | lock regression measurements |
| Lifetime / Safety | 07–10 | prevent UAF, restart bugs, unsafe storage |
| Shape / Dynamics Correctness | 11–14 | valid geometry and rigid-body math |
| Contact / Solver Correctness | 15–20 | normal, friction, restitution, iterations, stabilization |
| Stable Box Contacts | 21–23 | feature-based multi-point manifolds |
| CCD / Broad-Phase Correctness | 24–26 | pure prediction, valid TOIs, rotation-aware sweep |
| GJK / EPA Robustness | 27–28 | bounded and observable collision algorithms |
| Runtime Mutation | 29–30 | absolute geometry and transform synchronization |
| Rejected Timing Attempt | 31 | historical rejected fixed-timestep implementation |
| Corrective Solver Stability | 32 | establish fixed-rate contact stability before rescheduling |
| Fixed Scheduling | 33 | reintroduce fixed timestep on the approved Phase 32 solver |
| Sleeping / Islands | 34–36 | sleeping primitives, deterministic islands, sleep/wake integration |
| Core Performance | 37–42 | solver, manifold, EPA, support, GJK cost |
| Specialized Collision | 43–44 | faster primitive paths |
| Broad Phase | 45 | tune only if benchmark justifies |
| Consolidation | 46 | final measured audit |
| Parallelism | 47 | optional, only after all prerequisites |
# 22. Final Rule

At the end of every phase, Codex must stop at human review.

There is no automatic phase progression.

The only valid transitions are:

```text
Human PASS
    -> APPROVED
    -> commit
    -> tag
    -> push branch
    -> push tag
    -> stop

Human requests changes
    -> FIX PENDING
    -> revise same phase
    -> rerun validation
    -> AWAITING HUMAN REVIEW
    -> stop

Human REJECT
    -> rollback to previous approved tag
    -> REJECTED / ROLLED BACK
    -> stop
```

No next phase begins until the human explicitly instructs Codex to begin it.

Additional numbering rule:

```text
One phase number -> one authoritative objective.
```

Historical rejected objectives remain recorded but are not silently re-used after their number has been reassigned. If an unexpected corrective phase is inserted, all later unapproved phases must be formally renumbered in this plan before execution continues.
