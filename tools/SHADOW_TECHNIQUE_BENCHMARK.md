# Shadow technique benchmark - Phase 60

Decision: **retain the approved layered geometry-shader path**.

Baseline: `a39bc2290ff53d234d8676cc54a77068f61939c0`
(`render-refactor-phase-59-approved`). Both temporary alternatives passed exact
images but missed the selection thresholds recorded before measurement.
No production technique or architecture change was made.

## Compared techniques

| Method | Implementation tested | Compatibility / maintenance |
| --- | --- | --- |
| layered | Existing five-invocation cascade GS and six-face loop point GS; union lists and per-instance masks | Existing backend, grouped draws, layered attachment lifetime and ABI unchanged. |
| per-layer | Separate exact cascade/face lists and draws; vertex-stage projection, no GS; single-layer attachment with transactional restoration | Runs on the maintained OpenGL 4.6 context. More groups, duplicate instance data, attachment transitions and restoration; no new public API in the prototype. |
| alternate-layered | Cascade GS loops five layers; point GS uses six invocations | Same context, attachments, grouping and ABI; more GS invocations in these workloads without a demonstrated benefit. |

Every method uses the same resources, culling, masks, material coverage, projections,
main pass and frame. Per-layer logical shadow items count repeated layer draws;
layered items count a caster once per light.

## Matched protocol

NVIDIA GeForce RTX 3070 Laptop GPU; OpenGL 4.6 / NVIDIA 552.44. Hidden owned context,
camera (0,2,8) toward origin, FOV 45 degrees, aspect 1, near .1/far 20. Fixed
directional and range-12 point light. Targets: 64x64 linear RGBA8, 256x256 depth
with five active cascades and six point faces. Directional storage retains the
existing sixth, unused layer; the exact depth comparison includes its clear values.
No Physics, picking, swap or per-frame
glFinish. Engine pass timing is disabled; the fixture owns matched elapsed queries.

Two deterministic 64-caster workloads:
- sparse-boxes: 16 nearby boxes and 48 distant spheres;
- dense-spheres: 64 nearby spheres at fixed positions and scale.

Three processes per method; method order rotates by series. Each workload uses
120 warmup and 240 measured submissions, both shadows forced dirty. CPU timing
covers Submit including preparation/uploads; GPU elapsed covers that region.
Query collection, readback and a separate geometry-counter submission are outside
CPU samples. No tail cause is inferred.

Predeclared selection: exact images, existing-context compatibility, at least 10%
lower GPU median AND p95 on both workloads in all three series, no more than 5%
higher CPU median, and run-median spread at or below 10%. Thresholds were unchanged.
Results apply to these workloads and GPU, not a universally best technique.

## Timing

Milliseconds; values are the median of three per-run medians or per-run p95s.
NOISY means GPU run-median spread exceeds 10%.

| Workload | Method | CPU median | CPU p95 | GPU median | GPU p95 | GPU spread |
| --- | --- | --- | --- | --- | --- | --- |
| sparse-boxes | layered | 0.4369 | 0.4909 | 0.0430 | 2.9839 | within threshold |
| sparse-boxes | per-layer | 0.4917 | 0.5637 | 0.1106 | 1.6302 | NOISY |
| sparse-boxes | alternate-layered | 0.4360 | 0.4800 | 0.0666 | 2.8426 | NOISY |
| dense-spheres | layered | 0.4567 | 0.5017 | 1.1428 | 1.1592 | within threshold |
| dense-spheres | per-layer | 0.5284 | 0.6002 | 0.6282 | 1.2268 | NOISY |
| dense-spheres | alternate-layered | 0.4587 | 0.5211 | 1.2677 | 1.3005 | within threshold |

Per-layer CPU medians increase about 13% (sparse) and 16% (dense). Its dense GPU
median improves, but dense GPU spread is 46.4%; sparse GPU median worsens with
10.2% spread. The alternate GS layout has 47.7% sparse GPU spread and a slower
dense GPU result. Neither clears the thresholds. A lower tail in one workload
does not establish an overall win.

## Exact images and geometry work

All nine processes pass driver, finite-depth, coverage and static-cache checks.
Every method/series produces byte-identical cascade depth, all six point-face
depth images and final RGBA8 images for each workload. Phase 60 compares whole
binary artifacts exactly; the copied probe's older composition-tolerance banner
does not relax this gate.

API/primitive/vertex counters include the unchanged color pass. Shadow items count
actual logical draw instances; layer items count relevant memberships. Generated
primitives are measured after generation, before clipping. Zero GS counters in
per-layer mean no GS runs.

| Workload | Method | API draws | Shadow items | Layer items | Generated primitives | VS invocations | GS invocations | GS emitted primitives |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| sparse-boxes | layered | 3 | 32 | 42 | 696 | 1728 | 1152 | 504 |
| sparse-boxes | per-layer | 5 | 42 | 42 | 696 | 2088 | 0 | 0 |
| sparse-boxes | alternate-layered | 3 | 32 | 42 | 696 | 1728 | 1344 | 504 |
| dense-spheres | layered | 3 | 128 | 173 | 1941504 | 4718592 | 3145728 | 1417216 |
| dense-spheres | per-layer | 7 | 173 | 173 | 1941504 | 5824512 | 0 | 0 |
| dense-spheres | alternate-layered | 3 | 128 | 173 | 1941504 | 4718592 | 3670016 | 1417216 |

Sparse shadows submit 384 input triangles in either layered method and emit 504
(1.3125x); per-layer submits 504 without GS amplification. Dense shadows submit
1,048,576 layered input triangles and emit 1,417,216 (1.3515625x); per-layer repeats
those 1,417,216 inputs. Measured primitive totals and exact depth agree across
methods. Per-layer increases vertex work and API draws (3 to 5 / 3 to 7).

## Evidence and disposition

Local evidence: `logs/rendering/phase60/`.
- `protocol.json`: thresholds, workloads, warmup and sampling.
- `build-results.json` / `isolated-baseline.binlog`: exact build commands/exits.
- `measure-results.json` / `measurements/*/samples.csv`: nine runs and raw samples.
- `summary.json`: all 18 per-workload summaries, hashes, spreads and paired ratios.
- `consumers-results.json` / `consumer-closure.json`: Debug/Release native-free consumers.
- `evidence/*.cpp.txt` / `*.patch`: text snapshots of evaluated sources.
- `prototype-removal-manifest.json` / `prototype-removal-result.json`: removed runnable artifacts.
- `decision.json`: retained method and limitations.

The initial local Release library differed from its earlier recorded hash. An
isolated Release baseline was built from approved source with VS2022/v143
14.44.35207, SDK 10.0.26100.0, C++23 and static CRT. All comparison libraries share
that baseline with exactly one FrameSubmission object replaced under identical
options. Existing workspace binaries were not overwritten.

Runnable prototype source copies, variant libraries and executables were removed.
Text snapshots, patches, binary images, logs, commands and hashes remain as evidence.
No alternative or runtime technique switch enters production.

The Phase 59 p95 investigation and three optimization candidates remain deferred
to the Post-Rendering Performance Audit after Phase 68
(`docs/rendering/POST_RENDERING_PERFORMANCE_AUDIT.md`). This comparison does not
reopen Phase 59 or authorize a general latency-speedup claim.
