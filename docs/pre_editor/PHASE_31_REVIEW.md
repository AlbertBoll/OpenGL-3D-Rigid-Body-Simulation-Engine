# PRE_EDITOR Phase 31 Movers · Candidate 01

**NOT READY for Owner RBS acceptance.** Native exceeded the frozen rider/platform penetration and recovery limits in both replays. Jolt qualified for the admitted workload. Both Editor sessions remain unused. [Gate and resource ledger](../../logs/pre_editor/phase-31/candidate-01/status.json)

The candidate starts from approved Phase 30 commit `6f83ca3f8589bdbcbc9bc892c6fac22453eeb38c`. Its three exceptions retain their original scope. Phase 31 uses the Owner-admitted Chapter 23 workload `C31_SINELINE_RIDER_01` on both providers. The tracked local timing changes still match their entry hashes. [Admission](active/PHASE_31_ENTRY_ACCEPTANCE_01.json)

## Implementation and builds

The existing Physics fixed step computes a translation target from the signed phase tick. Jolt receives it through stock `BodyInterface::MoveKinematic`. Native receives the corresponding interval velocity before contact processing. The world publishes the mover sample and commits queued commands after a successful step. The box rider remains an independent dynamic body.

The RBS Movers scene exposes path and body authoring, Start, Stop, Reverse, effective readbacks and debug overlays. Its thumbnail is the actual overlay-free Jolt viewport at tick zero. The earlier blank Debug capture remains in evidence. Ordinary Editor interaction coverage is pending for all 485 frozen property rows. [Scene image and provenance](../../logs/pre_editor/phase-31/candidate-01/scene-thumbnail.json) · [Component matrix](../../logs/pre_editor/phase-31/entry-01/component-matrix.json)

| Isolated x64 profile | Runtime SHA-256 | Result |
| --- | --- | --- |
| Debug | `35198096aa4a6ff01177bdd92e1485748e63122ae9e89e5ef3fd3ec9234281cd` | GEngine and RBS compiled and linked. The first RBS compile exposed a fixture name collision. The corrected fixture passed. |
| Release | `b9c09f2273f179775dae0abb74a21a5baf985091dedd171f339652509a0e64be` | GEngine and RBS compiled and linked. Jolt and Native used this same executable. |

Build records include the source closure, original compile failure, compiler time and memory samples. The ordinary Owner build remains pending. [Build identities and source list](../../logs/pre_editor/phase-31/candidate-01/status.json)

## Qualification

| Check | Result | Evidence |
| --- | --- | --- |
| Debug safety, both providers | **PASS** | 12 attempted steps, 10 completions and two intentional invalid-duration rejections. Rollback, role restrictions, rider impulse, fresh identities and teardown passed. [Debug assessment](../../logs/pre_editor/phase-31/candidate-01/runs/debug/assessment.json) |
| Jolt Release | **PASS** | Two fresh 1,620-step replays agreed on every compared state and event. First support was tick 6. Peak penetration was 0.0033 mm. The rider slept at ticks 390 and 1,471 and woke at 541. [Final assessment](../../logs/pre_editor/phase-31/candidate-01/runs/jolt/assessment-02.json) |
| Native Release | **FAIL** | Two fresh 1,620-step replays reproduced the same contact violation. Platform trajectory, replay, bidirectional carry, sleep/wake and energy checks passed. [Native assessment](../../logs/pre_editor/phase-31/candidate-01/runs/native/assessment.json) |
| Editor and Owner | **PENDING** | The technical Editor role is held. The final Owner role remains reserved after technical READY. |

**Native contact.** At tick 8 the rider's lowest corner was **31.189 mm** below the platform top, against a **10 mm** limit. Its projection was 1.572 m inside the nearest platform edge. The first excursion beyond 5 mm started at tick 6. The recovery deadline was tick 10. The first two qualifying samples were ticks 69 and 70. A separate reconstruction from the eight rider corners confirmed both results in both schedules. [Independent geometry check](../../logs/pre_editor/phase-31/candidate-01/native-contact-crosscheck.json)

The Native rider's vertical velocity changed from −0.8333 m/s at tick 5 to −0.00235 m/s at tick 6. The first persistent manifold pair was reported at tick 7. The evidence establishes a contact response and excessive overlap. The exact solver operation responsible for the depth remains unresolved. [Native body and contact traces](../../logs/pre_editor/phase-31/candidate-01/runs/native/data/1)

**Release cost.** Inclusive Physics p95 was 0.0843 and 0.0744 ms on Jolt, and 0.0671 and 0.0560 ms on Native. Both schedules on both backends met the inclusive CPU and observer limits. Jolt used at most 12,459,616 Arena bytes and 14 positive reservations in one Update. Each replay recorded 18,474 positive reservations with matching releases, zero outstanding bytes at Update end and a matched job-page release at teardown. The initial assessment left six return addresses ambiguous. Offline disassembly resolved them against the pinned SDK and linked executable. [Initial assessment](../../logs/pre_editor/phase-31/candidate-01/runs/jolt/assessment.json) · [Allocation-site evidence](../../logs/pre_editor/phase-31/candidate-01/arena-source-review-01/verified-sites.json)

Native recorded whole-Update C++ allocations on 1,341 of 1,619 steady ticks per replay. The retained counters do not isolate the mover's incremental contribution. Native heap qualification remains **INCOMPLETE**. [Native cost and allocation results](../../logs/pre_editor/phase-31/candidate-01/runs/native/assessment.json)

## Execution boundary

Three of five process roles ran, with **6,492 actual attempts and 6,490 completions**. Their reservations total 6,504 of the 7,944-step ceiling. Each held Editor role retains its own 720-step reservation. The outstanding engineering decision is whether to admit targeted Native contact remediation and a separately bounded replacement run. [Resource ledger](../../logs/pre_editor/phase-31/candidate-01/status.json)
