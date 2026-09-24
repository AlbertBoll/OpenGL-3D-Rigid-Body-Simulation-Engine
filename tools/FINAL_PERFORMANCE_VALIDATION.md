# Phase 65 final performance validation

Status: authorized bounded correction validated and retained; Phase 65 remains BLOCKED and unsealed pending owner disposition. Historical thresholds and references are unchanged.

Baseline is approved Phase 64 (`01064633df6018af847fb901327e3911cec97872`). The authorized correction changes only `GEngine/src/Renderer/FrameSubmission.cpp`: exact cache decisions precede derived shadow preparation. The initial control measurements are retained below; the current corrected candidate and all remaining findings are reported in **Authorized corrective revision**. Application authoring, Physics, dependencies and approved baseline records remain unchanged.

## References and matching

- Common CPU frame/work/input/update/render, request counters and resource counts use `rendering-checkpoints/phase-13-baseline.json` and its retained evidence.
- CPU/GPU pass spans use `rendering-checkpoints/phase-48-baseline.json`, including its active one-box submission fixture. These measurements did not exist at Phase 13. Cached/unrequested passes are absent rather than zero-time samples; unavailable/abandoned GPU results are not zero.
- Match Ryzen 9 5900HX, RTX 3070 Laptop GPU and NVIDIA 552.44. Retain MSVC 14.44.35207, Windows SDK 10.0.26100.0, static CRT and Premake/VS2022. The approved C++20-to-C++23 gate is an intentional architecture change, not a reason to rebuild historical checkpoints with new settings.
- Four application workloads retain the baseline's fixed camera/layout, 1280x720 window, VSYNC=0, explicit 4096 shadows, zero update delta and no input. Use three independent processes, 120 warm-up frames and 240 samples each. Report actual target extents and multisampling. The separately defined active-pass fixture uses 64x64 color, 256 shadows, one box and its recorded camera/lights.
- Verify source/asset/workload settings against the reference. Explicitly enumerate intentional intervening algorithm, correctness, material and resource-policy changes. Do not divide timings from different scenes or extents and label the quotient a speedup. Approved old diagnostic images are not current visual goldens; repeated candidate scenes retain the same-driver 0.5% changed-pixel stability check in the recorded region.
- Do not run simultaneous GPU or CPU performance workloads. Capture inspection is separate from timing. Record binary/source hashes, process settings, actual exits and hardware. Keep Physics steps/time zero in render measurements; extraction, submission, GPU work and async preparation are separate scopes.

## Acceptance and noise thresholds

These thresholds are selected before final measurement:

| Metric | Threshold / disposition |
| --- | --- |
| Timing noise for Phase 13/48 and new three-process measurements | Run-median spread above 10% in either side is NOISY. Report median/p95/p99 and raw values; use stable work counters as the descriptive fallback. Do not claim speedup from noisy timing. Noise does not waive a reproducible material regression. |
| Matched common CPU frame/work/update/render | Investigate a stable median increase above 10% and 100 microseconds, or p95 increase above 20% and 200 microseconds. Changes below the absolute floor remain numerically reported. Frame pacing is included in frame_ns and must be distinguished from work_ns. |
| Matched per-pass CPU/GPU | Investigate a stable median increase above 10% and 10 microseconds, or p95 increase above 20% and 20 microseconds. Tiny empty-pass spans remain reported without presenting timer granularity as a material regression. |
| Work/resource counters | Explain every increase; require identical frozen signatures within a protocol unless an explicit event sequence defines the variation. Require zero Physics steps/time and zero steady target reallocations. Count scopes (including legacy failed-request counters and excluded external ImGui) must remain explicit. |
| Serial/parallel extraction | Keep the existing runner's paired 32/1024/8192-entity protocol: three series, four warmups and 21 samples/mode, modes serial/1/2/8, rotated order; 5% noise, at least 10% extraction gain and no more than 5% extraction-plus-visibility regression in each qualifying medium/large series. Serial remains the fallback. This narrower gate supersedes the general 10% noise rule for this fixture. |
| Async latency and frame stalls | Measure three series with fixed payloads/settings, report request-to-ready latency, request CPU, scheduler drain CPU and frame p95/max separately. Classify any observed frame over 16.667 ms as a stall and explain upload work and queue budgets; do not claim no stalls from mean latency. Warm OS file caches are explicit; no cold-disk claim. No new latency improvement is claimed without a matched historical metric. |
| Correctness / lifetime | No new GL errors, invalid workload/target identity, missing samples, failed resource publication, leaked resource owner or failed milestone application lifecycle. Keep deliberately injected failure tests distinct. |

A material regression needs a concrete explanation supported by measurements and explicit owner acceptance of the retained limitation, or a separately authorized bounded corrective revision with valid final evidence. An unnamed future phase is not a disposition. Mandatory unavailable tooling similarly requires explicit check-specific acceptance; no success is inferred from an attempted capture.

## Planned coverage

The four-application common/per-pass suite is supplemented with existing paired instancing, bind/upload, picking and shadow-culling fixtures, real target storage observations and serial/parallel extraction. A focused async fixture will measure actual texture/mesh preparation and scheduler publication. RenderDoc must inspect representative captured draw state/resources separately from measurements. Debug/Release affected builds and four graphical application smokes establish the milestone integration/lifecycle checks.

All source and protected inputs are captured before their applicable checks and verified unchanged before sealing. Long logs/raw samples remain under `logs/rendering/phase65/`; final results and limitations will be summarized here and in the current Phase 65 review.

## Initial untouched-control results

Evidence root: `logs/rendering/phase65/`. Corrected comparison: `final/corrected/comparison.json`. Complete numeric samples and run spreads remain in the referenced JSON/CSV files. All durations below are milliseconds unless stated otherwise.

### Workload identity and limits

The common comparison uses `final/common-v3/`, with per-pass timing disabled. The pass comparison uses `final/Release/phase-48-baseline.json`, with timing enabled; that historically named candidate output does not replace the tracked Phase 48 reference. The first exploratory `final/comparison.json` mixed pass instrumentation into the common comparison and is superseded.

Phase 13 explicitly disabled `BaseApp::Render` virtual `ImGuiRender` authoring. Current editor authoring would instead resize the scene to 880x636 with 8 samples. A separate validation executable suppresses only `SceneApp::ImGuiRender` to reproduce the old 1280x720 scene and default 16 samples; the current engine library is unchanged. Source and compile records are in `final/common-v3/editor-phase13/`. The resolved scene BMP and completed runtime metadata prove its extent; `RenderTarget::Create(width,height)` retains the explicit 16-sample default. Production editor behavior was separately exercised in both configurations and the Phase 48 comparison. The initial unadapted and missing-baseline-definition attempts remain in `final/common/` and `final/common-v2/`; neither contributes accepted timing.

All four current per-pass targets match Phase 48 storage descriptors after ignoring numeric framebuffer names: Editor 880x636/8x; simulation 880x636/16x; Breakout 1280x720/16x; RayTracing 880x636/16x. Windows are 1280x720/VSYNC off. Shadows remain 4096. `identity-verification.json` verifies all 115 Phase 13 and 66 Phase 48 referenced artifact hashes, 1,158 unchanged tracked source/asset/tool inputs, 45 protected paths and the original empty index. Repeated common workload counters are exact and diagnostic regions pass the existing 0.5% bound.

These are end-to-end architecture comparisons, not isolated algorithm speedups. Intervening approved changes include complete framebuffer storage/formats and non-square dimensions, typed resources/samplers, immutable frame extraction/retention, pass caching, bind-state enforcement, packed uploads, instancing, caster culling and shader correctness. Physics steps/time remain zero. CPU wall spans can include driver waits; GPU clocks/power were not locked or continuously sampled. RayTracing render time includes its CPU ray computation. Phase 13 had no separate extraction/pass GPU metric.

### Common CPU comparison against Phase 13

`frame` includes the existing 16 ms pacing. `work = input + update + render`; input/readback/Physics and full minimum/maximum/spread values remain in the JSON. A NOISY row cannot support a timing improvement claim.

| Application | Scope | Baseline median / p95 / p99 | Candidate median / p95 / p99 | Median change | Gate |
| --- | --- | --- | --- | --- | --- |
| GEngineEditor | frame | 16.3317 / 16.9326 / 17.4100 | 16.8557 / 18.0105 / 18.1688 | +3.2% | WITHIN_THRESHOLD |
| GEngineEditor | work | 1.4469 / 2.0146 / 2.4404 | 1.1647 / 1.4764 / 1.6957 | -19.5% | NOISY |
| GEngineEditor | update | 0.4185 / 0.6749 / 0.7852 | 0.3656 / 0.4467 / 0.5019 | -12.6% | NOISY |
| GEngineEditor | render | 0.9338 / 1.3937 / 1.7160 | 0.7713 / 1.0668 / 1.2749 | -17.4% | NOISY |
| RigidBodySimulation | frame | 16.3373 / 16.9634 / 17.1055 | 17.2768 / 17.9454 / 18.4516 | +5.8% | WITHIN_THRESHOLD |
| RigidBodySimulation | work | 1.0418 / 1.7651 / 2.2262 | 2.9381 / 3.3533 / 3.8660 | +182.0% | REVIEW |
| RigidBodySimulation | update | 0.0633 / 0.1157 / 0.1563 | 0.0549 / 0.0816 / 0.1022 | -13.3% | WITHIN_THRESHOLD |
| RigidBodySimulation | render | 0.9576 / 1.6058 / 2.0560 | 2.8333 / 3.2302 / 3.7542 | +195.9% | REVIEW |
| Breakout | frame | 16.0801 / 16.9768 / 17.1701 | 17.0645 / 17.5780 / 17.9291 | +6.1% | WITHIN_THRESHOLD |
| Breakout | work | 0.1883 / 0.3910 / 0.4716 | 0.5250 / 0.8427 / 1.1484 | +178.7% | NOISY |
| Breakout | update | 0.0094 / 0.0282 / 0.0390 | 0.0103 / 0.0203 / 0.0306 | +9.6% | NOISY |
| Breakout | render | 0.1544 / 0.3122 / 0.3857 | 0.4948 / 0.8112 / 1.1034 | +220.5% | NOISY |
| RayTracing | frame | 16.2199 / 17.2193 / 18.2998 | 16.9208 / 17.6986 / 18.6733 | +4.3% | WITHIN_THRESHOLD |
| RayTracing | work | 5.5011 / 8.2257 / 8.8869 | 5.5545 / 6.4002 / 7.2711 | +1.0% | WITHIN_THRESHOLD |
| RayTracing | update | 0.0007 / 0.0015 / 0.0025 | 0.0045 / 0.0080 / 0.0134 | +542.9% | NOISY |
| RayTracing | render | 5.4802 / 8.1670 / 8.8347 | 5.5312 / 6.3668 / 7.2290 | +0.9% | WITHIN_THRESHOLD |

### Common work and resources

Each cell is Phase 13 -> Phase 65 per frame, or steady live count. Counters observe first-party wrapper calls; third-party ImGui calls and some texture transfers are excluded. These are not complete driver invocation or memory totals.

| Counter | Editor | Simulation | Breakout | RayTracing |
| --- | --- | --- | --- | --- |
| draws | 342 -> 342 | 746 -> 8 | 109 -> 109 | 0 -> 0 |
| triangles | 321770 -> 321770 | 5902588 -> 1476668 | 218 -> 218 | 0 -> 0 |
| program_binds | 6 -> 13 | 7 -> 12 | 109 -> 112 | 0 -> 4 |
| vao_binds | 8 -> 15 | 746 -> 11 | 109 -> 112 | 0 -> 4 |
| texture_binds | 15 -> 23 | 1295 -> 19 | 109 -> 112 | 1 -> 5 |
| sampler_binds | 0 -> 37 | 0 -> 13 | 0 -> 221 | 0 -> 4 |
| fbo_binds | 4 -> 13 | 10 -> 13 | 1 -> 4 | 1 -> 5 |
| buffer_upload_calls | 0 -> 0 | 6 -> 0 | 0 -> 0 | 0 -> 0 |
| buffer_upload_bytes | 0 -> 0 | 384 -> 0 | 0 -> 0 | 0 -> 0 |
| shadow_passes | 0 -> 0 | 2 -> 0 | 0 -> 0 | 0 -> 0 |
| picking_passes | 0 -> 0 | 1 -> 0 | 0 -> 0 | 0 -> 0 |
| target_reallocations | 0 -> 0 | 0 -> 0 | 0 -> 0 | 0 -> 0 |
| buffers | 160 -> 160 | 90 -> 98 | 90 -> 90 | 90 -> 90 |
| vaos | 35 -> 35 | 24 -> 31 | 24 -> 24 | 24 -> 24 |
| textures | 28 -> 28 | 28 -> 27 | 14 -> 14 | 10 -> 10 |
| samplers | 0 -> 2 | 0 -> 2 | 0 -> 1 | 0 -> 0 |
| fbos | 6 -> 6 | 6 -> 6 | 6 -> 6 | 6 -> 6 |
| rbos | 1 -> 1 | 1 -> 1 | 1 -> 1 | 1 -> 1 |
| programs | 5 -> 5 | 8 -> 11 | 1 -> 1 | 0 -> 0 |
| estimated_buffer_bytes | 28010368 -> 28010368 | 3054936 -> 2965632 | 1596296 -> 1596296 | 1596200 -> 1596200 |

Simulation now submits 184 opaque items in five color draws, plus sky and two helpers: eight observed draws. Its cached shadow and unrequested picking passes are absent in steady samples. The triangle reduction therefore combines visibility/pass reuse with instancing; instancing alone does not reduce geometry. No buffer store upload or target reallocation occurs in any steady common workload. Uniform APIs and excluded texture APIs are not implied to be zero.

Bind increases are expected work introduced by explicit sampler separation and the scheduler/pass/legacy/UI state boundaries, including restoring state after external drawing. Simulation adds eight buffers, seven VAOs and three programs for its modern submission/resource/helper owners; two sampler owners replace texture-object sampling state. Legacy Editor/Breakout sampler owners and their repeated bind/reset calls are retained under Phase 64. These are fixed steady counts, not accumulation. Current ownership/retirement regression checks pass; detailed allocation origin and total driver memory are not inferred from wrapper counters.

### Per-pass comparison against Phase 48

Cached/unrequested passes are absent. UI/present have no valid GPU timing scope. All reported rows have the same named pass and workload; shaders and scheduling include the intentional intervening architecture changes.

| Workload / pass | Scope | Baseline median / p95 / p99 | Candidate median / p95 / p99 | Gate |
| --- | --- | --- | --- | --- |
| GEngineEditor / editor-ui | CPU | 0.6037 / 0.9595 / 1.2992 | 0.4060 / 0.5121 / 0.5646 | NOISY |
| GEngineEditor / legacy-scene | CPU | 0.5071 / 0.7063 / 0.8113 | 0.4036 / 0.6093 / 0.8791 | NOISY |
| GEngineEditor / legacy-scene | GPU | 0.3318 / 0.8632 / 1.6312 | 0.9626 / 3.0065 / 3.1027 | REVIEW |
| GEngineEditor / present | CPU | 0.0872 / 6.4564 / 11.2265 | 0.0418 / 0.0675 / 0.0861 | NOISY |
| GEngineEditor / resolve | CPU | 0.3336 / 0.6856 / 1.0149 | 0.0592 / 0.0879 / 0.1089 | WITHIN_THRESHOLD |
| GEngineEditor / resolve | GPU | 0.3246 / 8.0200 / 13.4625 | 0.1434 / 0.3205 / 0.4710 | NOISY |
| RigidBodySimulation / debug | CPU | 0.0319 / 0.0483 / 0.0632 | 0.0445 / 0.0574 / 0.0801 | NOISY |
| RigidBodySimulation / debug | GPU | 0.0010 / 0.0020 / 0.0020 | 0.0010 / 0.0020 / 0.0020 | WITHIN_THRESHOLD |
| RigidBodySimulation / editor-ui | CPU | 0.2324 / 0.3745 / 0.5512 | 0.2993 / 0.3564 / 0.4416 | REVIEW |
| RigidBodySimulation / masked | CPU | 0.0046 / 0.0125 / 0.0256 | 0.0011 / 0.0012 / 0.0018 | NOISY |
| RigidBodySimulation / masked | GPU | 0.0010 / 0.0020 / 0.0020 | 0.0010 / 0.0020 / 0.0020 | WITHIN_THRESHOLD |
| RigidBodySimulation / opaque | CPU | 1.7484 / 2.1636 / 2.8745 | 0.2432 / 0.3188 / 0.3566 | WITHIN_THRESHOLD |
| RigidBodySimulation / opaque | GPU | 2.8344 / 8.5484 / 9.7597 | 3.7366 / 4.4308 / 4.7432 | REVIEW |
| RigidBodySimulation / present | CPU | 1.7526 / 9.3116 / 14.4468 | 0.0367 / 0.0572 / 0.0795 | WITHIN_THRESHOLD |
| RigidBodySimulation / resolve | CPU | 0.3093 / 0.5167 / 0.6304 | 0.0570 / 0.0816 / 0.1032 | WITHIN_THRESHOLD |
| RigidBodySimulation / resolve | GPU | 0.0625 / 4.1892 / 9.4833 | 0.0778 / 0.0911 / 0.0973 | REVIEW |
| RigidBodySimulation / skybox | CPU | 0.0237 / 0.0395 / 0.0533 | 0.0401 / 0.0514 / 0.0656 | NOISY |
| RigidBodySimulation / skybox | GPU | 0.0010 / 0.0020 / 0.0020 | 0.0010 / 0.0020 / 0.0020 | WITHIN_THRESHOLD |
| RigidBodySimulation / transparent | CPU | 0.0009 / 0.0015 / 0.0019 | 0.0008 / 0.0009 / 0.0012 | NOISY |
| RigidBodySimulation / transparent | GPU | 0.0010 / 0.0020 / 0.0020 | 0.0010 / 0.0020 / 0.0020 | WITHIN_THRESHOLD |
| Breakout / legacy-scene | CPU | 0.2029 / 0.5135 / 0.6179 | 0.4239 / 0.6768 / 0.8789 | NOISY |
| Breakout / legacy-scene | GPU | 0.1556 / 0.4065 / 0.4188 | 0.7485 / 1.3793 / 2.0705 | NOISY |
| Breakout / present | CPU | 0.0980 / 6.4288 / 10.4444 | 0.0569 / 0.0845 / 0.1299 | WITHIN_THRESHOLD |
| RayTracing / editor-ui | CPU | 8.6098 / 11.2643 / 12.5070 | 5.3957 / 5.9732 / 7.3107 | WITHIN_THRESHOLD |
| RayTracing / present | CPU | 0.0931 / 5.6525 / 9.8992 | 0.0597 / 0.0769 / 0.1057 | WITHIN_THRESHOLD |
| SubmissionFixture / debug | CPU | 0.0005 / 0.0006 / 0.0008 | 0.0007 / 0.0008 / 0.0014 | WITHIN_THRESHOLD |
| SubmissionFixture / debug | GPU | 0.0010 / 0.0010 / 0.0020 | 0.0010 / 0.0010 / 0.0010 | WITHIN_THRESHOLD |
| SubmissionFixture / directional-shadow | CPU | 0.0387 / 0.1823 / 0.3033 | 0.0325 / 0.0385 / 0.0475 | WITHIN_THRESHOLD |
| SubmissionFixture / directional-shadow | GPU | 0.0123 / 0.0143 / 0.0215 | 0.0102 / 0.0123 / 0.0123 | NOISY |
| SubmissionFixture / masked | CPU | 0.0011 / 0.0013 / 0.0019 | 0.0008 / 0.0009 / 0.0015 | WITHIN_THRESHOLD |
| SubmissionFixture / masked | GPU | 0.0010 / 0.0010 / 0.0020 | 0.0010 / 0.0010 / 0.0010 | WITHIN_THRESHOLD |
| SubmissionFixture / opaque | CPU | 0.0234 / 0.0645 / 0.0921 | 0.0141 / 0.0156 / 0.0234 | WITHIN_THRESHOLD |
| SubmissionFixture / opaque | GPU | 0.0174 / 0.0205 / 0.0225 | 0.0164 / 0.0184 / 0.0195 | NOISY |
| SubmissionFixture / picking | CPU | 0.0210 / 0.0590 / 0.0744 | 0.0149 / 0.0164 / 0.0282 | WITHIN_THRESHOLD |
| SubmissionFixture / picking | GPU | 0.0092 / 0.0102 / 0.0113 | 0.0082 / 0.0102 / 0.0102 | NOISY |
| SubmissionFixture / point-shadow | CPU | 0.0196 / 0.0606 / 0.0807 | 0.0122 / 0.0146 / 0.0220 | WITHIN_THRESHOLD |
| SubmissionFixture / point-shadow | GPU | 0.0164 / 0.0195 / 0.0205 | 0.0174 / 0.0184 / 0.0195 | NOISY |
| SubmissionFixture / skybox | CPU | 0.0006 / 0.0008 / 0.0010 | 0.0009 / 0.0010 / 0.0016 | WITHIN_THRESHOLD |
| SubmissionFixture / skybox | GPU | 0.0010 / 0.0010 / 0.0020 | 0.0010 / 0.0010 / 0.0010 | WITHIN_THRESHOLD |
| SubmissionFixture / transparent | CPU | 0.0005 / 0.0006 / 0.0008 | 0.0007 / 0.0008 / 0.0014 | WITHIN_THRESHOLD |
| SubmissionFixture / transparent | GPU | 0.0010 / 0.0010 / 0.0020 | 0.0010 / 0.0010 / 0.0010 | WITHIN_THRESHOLD |

### Submission, picking, shadows and uploads

Existing paired fixtures keep their frozen scenes, exact output/work checks and three 120-warmup/240-sample series. Complete series/tails: `final/Release/results.json` and raw CSV files.

| Fixture | Observed result |
| --- | --- |
| Instancing, 256 logical instances | Serial 256 draws -> instanced 4 in both sphere and box workloads; exact geometry/identity and color/picking/shadow checks pass. |
| sphere-lattice, serial | CPU run medians 3.5638, 3.5008, 3.4112; p95 4.4331, 4.4914, 3.6500. GPU medians 1.3348, 1.1909, 1.1919. |
| sphere-lattice, instanced | CPU run medians 0.4256, 0.3932, 0.4109; p95 0.6560, 0.4469, 0.4813. GPU medians 1.2605, 1.2554, 1.2616. |
| box-stack, serial | CPU run medians 0.5618, 0.5824, 0.5793; p95 0.6454, 0.6729, 0.6192. GPU medians 0.0466, 0.0502, 0.0502. |
| box-stack, instanced | CPU run medians 0.3216, 0.3190, 0.3329; p95 0.3940, 0.3562, 0.3635. GPU medians 0.0389, 0.0389, 0.0389. |
| Shadow culling | 128 -> 32 caster submissions, 704 -> 42 layer emissions, 5 -> 3 instanced draws; exact depth/color equality. Reference CPU median 0.4302 -> culled 0.4455. GPU run-median center 1.4787 -> 0.0317, but culled p95 3.1232-3.1836 exceeds reference 1.4940-1.7306: retain the tail tradeoff, not a universal win. |
| Picking | Idle: zero pass/items/readbacks. Cached click: one read, zero pick items, medians 0.0222-0.0224/p95 0.0235-0.0271. Dirty click: 64 items/one read, medians 1.3129-1.3760/p95 1.6819-1.8438. Existing gate does not select a PBO; no historical latency improvement is claimed. |
| Upload/bind fixture | 64 fixed draws; Submit CPU run medians 0.3099/0.2874/0.2853, p95 0.3849/0.3071/0.3081. 768 uniform calls/6912 bytes; zero buffer calls/bytes after warmup. Program/VAO/texture/sampler calls 5/17/14/7. This run measures the candidate; no old-library reduction claim is made. |
| State-cache fixture | 256 draws, run median CPU 0.7247/0.6768/0.6808; stable driver-call signatures and equal image hashes. Program/VAO/texture/sampler/FBO calls 5/2/8/7/8. |
| Shadow storage | At 4096: six allocated directional layers plus six cube faces, sized 32-bit depth = 805306368 bytes (768 MiB), matching Phase 13. Five directional cascades are used; allocation still contains six layers. 1024/2048/custom-333 actual storage also queried: 48/192/5.0761 MiB. Driver overhead is excluded. |

### Serial and parallel extraction

`final/extraction/benchmark.json`: exact checksums, three paired series, 4 warmups and 21 samples/mode. CPU scope is preparation + extraction + visibility, excluding submission, GPU and Physics. Medians/p95 below pool 63 samples; the actual admission gate uses each series independently.

| Entities | Workers (0 = serial) | Preparation median | Extraction median | Complete CPU median / p95 |
| --- | --- | --- | --- | --- |
| 32 | 0 | 0.0881 | 0.0096 | 0.1248 / 0.1353 |
| 32 | 1 | 0.0870 | 0.0067 | 0.1213 / 0.1292 |
| 32 | 2 | 0.0861 | 0.1093 | 0.2247 / 0.2555 |
| 32 | 8 | 0.0898 | 0.4570 | 0.5782 / 0.6187 |
| 1024 | 0 | 2.9465 | 0.2091 | 4.0196 / 4.3195 |
| 1024 | 1 | 3.0503 | 0.1139 | 4.0910 / 4.8050 |
| 1024 | 2 | 3.0206 | 0.3396 | 4.3321 / 4.8766 |
| 1024 | 8 | 3.0496 | 0.8690 | 4.9559 / 5.4093 |
| 8192 | 0 | 31.0422 | 2.2883 | 40.6088 / 45.1601 |
| 8192 | 1 | 33.4087 | 1.8362 | 42.6334 / 48.5986 |
| 8192 | 2 | 33.4871 | 2.1899 | 42.8435 / 48.2873 |
| 8192 | 8 | 33.5151 | 2.9226 | 43.6257 / 47.6392 |

No parallel mode qualifies in all required series. Keep serial default and the existing optional bounded path; preparation/visibility dominate the large fixture. The 8192-entity fixture exceeds a 16.667 ms CPU budget even serially; it is a scaling limitation, not an application-frame benchmark.

### Async latency and scheduler stalls

`final/async-v2/summary.json`: three series, two warmups and eight measured requests per asset; actual PNG decode/OBJ import and queue publication. Fresh loader/registry, warm OS cache, default two workers/64 MiB byte limits/2 ms between-upload budget. Invisible scheduler without simulation or scene drawing; it includes the normal window-state/present boundary and hidden-window swap. The original no-present description was incorrect; this scope clarification changes no raw sample. The 1 ms polling wait is excluded from scheduler CPU and included in ready latency. These are lower-bound publication costs, not full application frame durations.

| Payload / metric | Median | p95 | Maximum | Quality |
| --- | --- | --- | --- | --- |
| texture / request | 0.0047 | 0.0082 | 0.0124 | NOISY |
| texture / ready | 158.6959 | 165.9733 | 167.0395 | STABLE |
| texture / max_scheduler | 23.6784 | 30.0351 | 30.7638 | STABLE |
| texture / scheduler | 0.0404 | 0.0759 | 30.7638 | STABLE |
| mesh / request | 0.0031 | 0.0037 | 0.0043 | STABLE |
| mesh / ready | 109.5923 | 115.9950 | 116.0972 | STABLE |
| mesh / max_scheduler | 0.3687 | 3.8354 | 4.4210 | NOISY |
| mesh / scheduler | 0.0388 | 0.0738 | 4.4210 | STABLE |

Texture queue payload is 16777232 bytes; all 24 measured texture requests each caused one scheduler frame above 16.667 ms (maximum 30.764 ms). Mesh payload is 288392 bytes; none of 24 requests caused such a stall (maximum 4.421 ms). Each request published once on the owning context thread and retired its CPU payload/resources; no GL error occurred. The 2 ms queue budget is checked between indivisible uploads and cannot cap one large texture upload. No no-stall or cold-disk claim is justified.

### RenderDoc and integration

Debug and Release builds, consumer boundaries, complete submission/shadow/cross-pass regression suites and all four graphical application native-close/responsiveness smokes passed. Evidence: `final/Debug/results.json`, `final/Release/results.json`. The deliberately injected GL error in the fixture is an expected negative control, not an unexplained runtime error.

RenderDoc 1.46 captured and replayed `renderdoc/direct-v2/direct_capture.rdc`. Normal external injection crashed before connecting in 1.46 and signed fallback 1.36; those failed attempts and OS diagnostics are retained. A temporary copy of the existing fixture loads the official API before context creation and captures iteration 120; production source/library and all timing runs remain unchanged. `renderdoc/direct-v2/{compile,application}.json` record successful exits.

`renderdoc/direct-replay-1.46/inspection.json` verifies four 36-vertex triangle draws: directional/point at 256x256, picking/color at 64x64; back-face culling, depth Less with writes, blending disabled, signed R32_SINT picking, RGBA8 color, six-layer D32 directional and six-face D32 cubemap targets. No replay debug messages. The saved color image was inspected and contains the centered one-box scene. Each shadow target contains 1572864 bytes. The capture validates the representative active-pass fixture, not every application or an unrestricted visual golden.

### Initial findings carried into corrective review

The owner authorized the bounded corrective revision documented below. These initial findings remain the starting record; no limitation acceptance is inferred from that authorization, Phase 64 approval or successful correctness tests.

1. Simulation common render CPU median 0.9576 -> 2.8333 ms (+195.9%); work 1.0418 -> 2.9381 ms (+182.0%), with exact workload counters. The current opaque submission CPU span improves 1.7484 -> 0.2432 ms against Phase 48, so fewer draw calls do not establish lower whole-render cost. Extraction/resource freeze and work outside named pass spans remain included in whole render; a causal allocation of the extra CPU cost has not been isolated.

2. Stable Phase 48 GPU/pass regressions: Editor legacy-scene median 0.3318 -> 0.9626 ms and p95 0.8632 -> 3.0065; simulation opaque GPU median 2.8344 -> 3.7366 ms; resolve GPU 0.0625 -> 0.0778 ms; UI CPU 0.2324 -> 0.2993 ms. Simulation opaque/resolve tails improve, but the median gates still fail. Shader/material correctness and state policies changed; no single causal attribution or GPU speedup is claimed.

3. Breakout common render median 0.1544 -> 0.4948 ms and per-pass increases are NOISY; repeated current runs do not justify dismissing the higher costs. Editor common timing and simulation debug/sky CPU also retain noise limits. Accepting this packet accepts the unresolved uncertainty, not proof of equivalence.

4. Async texture publication stalls up to 30.764 ms under the default indivisible-upload policy; shadow-culling GPU p95 tail increase despite less work; serial-default extraction and the large-fixture CPU scaling limit above.

These measurements preceded the authorized correction below. Historical baselines and approved state remain unchanged; no Phase 66 work, commit, tag or publication is included.

## Authorized corrective revision (current candidate)

Owner authorization covers the complete proposal in `docs/rendering/reviews/PHASE_65_CORRECTIVE_PROPOSAL.md`, including conditional implementation. The immutable authorized proposal and instruction record are `correction/authorized-proposal.md` and `correction/authorization.json` under the evidence root. One diagnostic batch per CPU/GPU/async domain was used; no additional batch was used. Final validation is the separately required Step 2 coverage, not another attribution search.

### Step 1 attribution and intervention gate

`correction/cpu/batch-1/summary.json` pairs an untouched executable with private instrumented source copies, three processes each, 120 warmups/240 samples, pass timing off. Absolute matched observer differences were 0.11730, 0.02925 and 0.00285 ms, each within max(5%, 0.050 ms). Both sets of whole-render medians are stable. Every measured frame reused both shadows. Shadow preparation medians were 0.54520 / 0.52500 / 0.52945 ms, or 19.096% / 18.043% / 18.133% of whole render. **The >=10% AND >=0.100 ms intervention gate passed in all three series before production edits.**

The following are same-frame diagnostic scopes, in ms. Parent scopes include their children; medians are not additive. Remainders were computed within individual frames before aggregation.

| Scope | Median | p95 | Quality |
| --- | --- | --- | --- |
| Whole render | 2.88860 | 3.81390 | STABLE |
| Update resources | 0.00075 | 0.00240 | NOISY |
| Extraction call (parent) | 0.98830 | 1.31780 | STABLE |
| Resource preparation (child) | 0.92450 | 1.26530 | STABLE |
| Extraction (child) | 0.05825 | 0.07580 | STABLE |
| Submit (parent) | 1.41030 | 1.88730 | STABLE |
| Submission setup | 0.00690 | 0.01190 | STABLE |
| Visibility | 0.19935 | 0.24130 | STABLE |
| Shadow matrices | 0.00220 | 0.00370 | STABLE |
| Shadow caster lists | 0.52930 | 0.64340 | STABLE |
| Exact pass keys | 0.18875 | 0.25280 | STABLE |
| Ordering/instance planning | 0.02650 | 0.04100 | STABLE |
| Material/frame packing | 0.00820 | 0.01640 | STABLE |
| Upload/binding setup | 0.05600 | 0.29510 | STABLE |
| Submit remainder: pass draws/cleanup | 0.35140 | 0.63700 | STABLE |
| Resolve | 0.07345 | 0.10220 | STABLE |
| UI | 0.33285 | 0.53560 | STABLE |
| Present | 0.04400 | 0.08690 | NOISY |
| Frame/resource retirement | 0.02030 | 0.03300 | NOISY |
| Scheduler remainder | 0.00265 | 0.00770 | NOISY |
| Outside scheduler | 0.00360 | 0.01040 | NOISY |

Most redundant shadow cost is list construction. Resource preparation, visibility and exact keys remain separate substantial costs; none is changed by this authorization. Stage timing locates current work, not a complete causal decomposition against Phase 13, which had no such stage metrics.

### Conditional correction and final admission

`FrameSubmission.cpp` now computes the unchanged exact invalidation decision first. Directional/point matrices and caster lists are built only for executed shadow passes. Cache hits reuse value-only light/layer/caster statistics and set current submitted counts to zero; they retain no frame-local ordinal list or heavy asset. All original validation, key inputs, resource versions, pass ordering and failure retry reasons remain. Keys, cached matrices and visibility metadata commit only after the final successful submission check.

One implementation hypothesis and one refinement were used. Static review found that the existing `lastFrame` assignment still published matrix metadata before draw success; the refinement moved it to the successful commit point. The earlier successful Debug run in `correction/final/Debug/` is retained but superseded for final-candidate coverage by `Debug-v2/`. No other production file changed.

Final admission: `correction/final/admission.json`. Paired common runs alternated control/candidate order. Both run-median spreads are below 2.2%; unchanged counters include eight draws, zero shadow/picking work, zero steady uploads/reallocations and zero Physics. Diagnostic changed fractions are 0.00105%-0.00230%, below the existing 0.5% bound.

| Paired series | Untouched render median | Corrected render median | Saving | Saving % |
| --- | --- | --- | --- | --- |
| 0 | 2.89330 | 2.38995 | 0.50335 | 17.40% |
| 1 | 2.91075 | 2.37240 | 0.53835 | 18.50% |
| 2 | 2.95520 | 2.36935 | 0.58585 | 19.82% |

| Aggregate scope | Untouched median / p95 / p99 | Corrected median / p95 / p99 |
| --- | --- | --- |
| render_ns | 2.91545 / 3.87120 / 4.13240 | 2.37515 / 3.28420 / 3.53700 |
| work_ns | 2.99050 / 4.01620 / 4.27490 | 2.45080 / 3.42890 / 3.68020 |

All three pairs exceed the required 10% and 0.100 ms improvement. Fresh untouched-control per-pass comparisons show no new stable median/tail regression; noisy CPU pass rows remain explicitly NOISY (`correction/final/control-passes/summary.json`). The four aggregate reference/culled CPU/GPU comparisons also remain within the unchanged gates (`shadow-control-comparison.json`). These admission results retain the correction; they do not accept historical regressions.

**Historical Phase 13 gates still fail:** corrected render median/p95 2.37515/3.28420 ms exceed 1.05755/1.92696 ms; work 2.45080/3.42890 ms exceeds 1.14598/2.11812 ms. The historical render median is 0.95755 ms. No threshold or reference was rewritten.

### GPU/UI and shadow evidence

Current Editor/simulation captures record shader source hashes, resource/combined-sampler bindings, pass state and target descriptors. Final simulation state matches the untouched capture for all eight engine draws; five additional draws are external UI. The resolved image changed fraction is 0.001073%. The final active fixture contains four draws, with expected directional/point depth, signed picking and color targets. All four replays have zero debug messages. Actual OpenGL combined samplers were read through descriptor-store ImageSampler accesses; the standalone sampler accessor is empty for those bindings. Evidence: `correction/final/capture-samplers/` and `capture-inspection/verification.json`.

Before-process GPU observations were RTX 3070 Laptop / 552.44: Editor P0, 1680/6000 MHz, 30.04 W, 63 C; simulation P5, 255/810 MHz, 25.67 W, 63 C. These are not steady-run or locked-clock measurements. They cannot assign the historical GPU/UI regression to a particular shader, driver wait or power state.

Fresh Phase 48 protocol results below are median / p95 / p99 ms. Complete numeric comparison is `correction/final/comparison.json`.

| Scope | Initial untouched Phase 65 | Corrected validation | Current historical numeric gate |
| --- | --- | --- | --- |
| Editor legacy GPU | 0.96256 / 3.006464 / 3.10272 | 0.169984 / 0.893952 / 6.201344 | WITHIN_THRESHOLD; long p99 retained |
| Simulation opaque GPU | 3.736576 / 4.430848 / 4.743168 | 1.814528 / 2.921472 / 8.877056 | WITHIN_THRESHOLD |
| Simulation resolve GPU | 0.077824 / 0.091136 / 0.097280 | 0.047104 / 0.063488 / 0.066560 | WITHIN_THRESHOLD |
| Simulation UI CPU | 0.29930 / 0.35640 / 0.44160 | 0.33515 / 0.54220 / 0.62880 | REVIEW |

The historical GPU numeric gates pass in this newer batch, but the large between-batch variation remains unexplained. The newly measured untouched simulation control also has low opaque/resolve GPU medians (1.844224/0.049152 ms); the Editor path is unchanged by the production correction. Therefore no GPU fix or causal GPU speedup is claimed, and the original GPU/UI finding remains OPEN. A within-batch stable median does not resolve uncertainty between batches.

The Step 1 shadow pair reproduces the tail: reference GPU p95 1.490944-1.494016 ms versus culled 3.214336-3.444736 ms. Final corrected culled p95 is 3.263488-3.741696 ms; reference p95 is 1.489920/7.137280/1.491968 ms, including the second-series outlier. Aggregate reference p95 1.741824 ms and culled 3.301376 ms remain within the control-comparison gate. All exact depth/color checks and 128->32 casters, 704->42 layers, 5->3 draws pass. The tail limitation remains OPEN.

### Supplementary async attribution

`correction/async/batch-1/` retains three processes, two warmups/eight requests per asset, the same payloads/queue limits and owner-thread publication. No synchronization was added inside samples. `glTexImage2D` combines storage/base transfer. `glGetTexLevelParameteriv` validation bears much of the blocking wall time and may charge preceding upload/mipmap GPU work; a quick mip call does not prove the GPU work completed.

| Texture metric | Median | p95 | Max | Quality |
| --- | --- | --- | --- | --- |
| Request | 0.00450 | 0.01040 | 0.01850 | NOISY |
| Ready latency | 167.39080 | 207.19690 | 214.77970 | NOISY |
| Per-request max scheduler | 25.28690 | 46.63870 | 55.35790 | NOISY |
| Storage/base transfer call | 3.10100 | 4.90430 | 5.47470 | NOISY |
| Mipmap call | 0.00030 | 0.00090 | 0.00120 | NOISY |
| Image/storage validation queries | 21.26875 | 42.05900 | 43.42530 | NOISY |
| State queries accumulated per request | 1.21965 | 116.25190 | 128.49740 | NOISY |

State-query totals span multiple polling frames and must not be read as one frame or one query. Same-publication-frame residual after upload-specific driver spans has median 1.27745 ms, p95 1.86830 ms, max 6.43800 ms (`publication-residual.json`). It is an upper bound for remaining publication CPU plus state queries/scheduling/hidden-window swap, not an isolated registry-publication duration.

All 24 texture requests again stall; instrumented scheduler maximum is 55.3579 ms. Mesh ready median/p95/max is 113.91215/123.02330/135.76880 ms; scheduler maximum 17.36910 ms includes one stall. Mesh buffer-transfer median is 0.02485 ms. These noisy supplementary timings do not establish a regression against the original uninstrumented 30.764 ms texture / 4.421 ms mesh maxima. They confirm that the retained policy does not guarantee a stall-free frame. No async production code changed.

### Final coverage and remaining disposition

Fresh Debug/Release builds, consumer-boundary gates, submission/invalidation/culling/instancing/picking/upload/cross-pass/retirement checks and all four graphical application smokes pass. The opt-in isolated preparation probe counts actual matrix/list computation and checks zero work on cache hits plus required recomputation for tested resource, transform, camera/light/settings/target, deferred, retained-version and failure/retry cases. It reports 885 directional matrix calls, 172 point-matrix calls and 177/172 list builds; production contains no observation hook. Exact cached visibility/submission counters and depth/color oracles pass. Evidence: `correction/final/{Debug-v2,Release,preparation-probe}/`.

Unchanged legacy common measurements, extraction and async evidence are explicitly reused by source/call-path identity. FrameSubmission is not entered by the invisible async scheduler or extraction-only benchmark. Final per-pass measurements and all four smokes were freshly repeated; serial extraction remains the selected fallback.

| Finding | Current disposition |
| --- | --- |
| Redundant shadow preparation on exact cache hits | RESOLVED within the authorized correction; intervention, correctness and retention admission passed. |
| Phase 13 simulation whole-render/work CPU regression | OPEN: useful partial improvement still fails all four historical thresholds. |
| Editor/simulation GPU and UI regressions | OPEN: latest GPU numeric gates pass, but between-batch GPU differences have no isolated cause; UI CPU still fails. |
| Noisy historical comparisons | OPEN: current repetitions do not repair Phase 13 reference noise; no equivalence claim. |
| Texture/mesh scheduler stalls | OPEN: indivisible upload policy retained; original and supplementary observations both remain visible. |
| Shadow-culling tails | OPEN: reduced work and exact images coexist with high/intermittent GPU tails. |
| Extraction scaling | OPEN: original serial/parallel gate still selects serial; no extraction implementation change. |

Phase 65 remains **BLOCKED, approval-ineligible and unsealed**, as instructed. No limitation is accepted. No approval, commit, tag, push or Phase 66 work occurred. Updated owner review: `docs/rendering/reviews/PHASE_65_REVIEW.md`.


## Build-closure follow-up (separate owner-requested revision)

The prior Release consumer/build PASS covered selected targets and public-header compilation; it did not cover a clean PhysicsTests Release link. The timing-build target list omitted PhysicsTests. A manual full rebuild exposed a pre-existing Phase 64-source dependency: enabled RenderCounters bookkeeping directly referenced SDL_GL_GetCurrentContext through retained renderer resource methods. Normal Release counters were disabled, and Debug PhysicsTests already delay-linked SDL; neither covered this instrumented Release combination.

The repair changes only the existing private counter/context bootstrap boundary. Context creation installs the current-context query; counter keys call it indirectly. PhysicsTests Release gains no SDL dependency, and no performance preparation, shader or Physics behavior is changed. A full instrumented Release solution rebuild, separate clean PhysicsTests link, SDL-import rejection, 18,984 CPU checks, scene-grouping checks, GL counters and four application smokes pass. Final Debug coverage and exact provenance are recorded in `docs/rendering/reviews/PHASE_65_BUILD_CLOSURE.md` and `logs/rendering/phase65/build-closure/`.

The completed cached-shadow correction and all preceding numeric evidence are preserved. Those measurements describe their recorded pre-build-repair inputs; they are not new timings of the repaired candidate. Phase 13 historical thresholds remain unchanged. CPU residuals, GPU/UI regressions/uncertainty, noisy historical comparisons, texture stalls, shadow tails and extraction scaling remain OPEN. This build repair does not accept any performance limitation. Phase 65 remains BLOCKED and unsealed.


## Phase 65 authorized resource-preparation attempt (review iteration 5)

Status: bounded attempt COMPLETE; conditional production correction NOT RETAINED. Step 1 and the complete semantic-key proof passed before production editing. The candidate passed correctness but failed the predeclared final whole-render retention gate. Only the new RenderState.cpp change was restored, byte-for-byte, to the verified post-build-closure source. Phase 65 remains BLOCKED and UNSEALED; earlier fixes are unchanged.

### Fresh control, bounded attribution and key proof

A fresh instrumented Release engine/simulation Rebuild supplied `logs/rendering/phase65/resource-preparation/control/`. All prior recorded source inputs matched. Current binary hashes differed from the prior build packet, so they were not reused; the accepted build repair was not reopened. Neither the historical `correction/control/GEngine.lib` nor the earlier 0.92450 ms preparation estimate served as the new control or claimed saving.

One fine CPU diagnostic batch: three alternating pairs, 120 warmup/240 sampled frames, existing frozen workload/layout, pass timing disabled, shadows 4096. Isolated RenderState.cpp instrumentation measured same-frame preparation substages. No production observer hook. Predeclared observer limit: absolute paired render median difference <=max(5%,0.050 ms), whole-render run-median spread <=10%. No coarser retry.

| Series | Control render ms | Diagnostic render ms | Observer difference ms | Direct duplicate Prepare+version construction ms | Fraction of diagnostic render | Step 1 gates |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 2.28845 | 2.35315 | 0.06470 | 0.34140 | 14.508% | PASS |
| 2 | 2.29850 | 2.40565 | 0.10715 | 0.40215 | 16.717% | PASS |
| 3 | 2.33810 | 2.40895 | 0.07085 | 0.40540 | 16.829% | PASS |

Every series exceeds 0.100 ms and 10% of BOTH the control and diagnostic medians. Whole-render run-median spread: control 2.160%, diagnostic 2.320%. All frames: 191 entities, 189 mesh entities, seven unique successful material versions, 182 duplicates, zero errors and 2,608 version entries. Direct duplicate medians vary 15.914%; the predeclared observer/noise gate is on whole-render medians, and each series independently exceeds intervention thresholds. No stable substage speedup is inferred.

| Diagnostic stage (ns names; values in ms) | Median | p95 | p99 |
| --- | --- | --- | --- |
| total_ns | 1.06295 | 1.20740 | 1.27280 |
| world_ns | 0.07960 | 0.10470 | 0.11650 |
| hierarchy_ns | 0.12885 | 0.15680 | 0.17520 |
| mesh_ns | 0.24535 | 0.25930 | 0.27010 |
| acquire_ns | 0.00790 | 0.00940 | 0.01030 |
| prepare_ns | 0.26485 | 0.30130 | 0.32610 |
| versions_ns | 0.14595 | 0.17020 | 0.18620 |
| containers_ns | 0.06705 | 0.09000 | 0.10440 |
| material_result_ns | 0.43360 | 0.48590 | 0.52950 |
| duplicate_prepare_ns | 0.25290 | 0.28580 | 0.30630 |
| duplicate_versions_ns | 0.13980 | 0.16380 | 0.17800 |
| duplicate_result_ns | 0.41380 | 0.46180 | 0.49480 |
| remainder_ns | 0.10050 | 0.13130 | 0.15480 |

Wrapper/substage timers overlap, and quantiles are not additive. The intervention estimate uses only duplicate Prepare plus version construction, excluding Acquire or speculative lookup/copy savings. Per-run median/p95/p99 and stage spread are in `fine-batch/per-run-quantiles.json`, derived from the same raw samples without another trial.

The complete logical key is `(one UpdateRenderState invocation, FrameAccess authority, material/program/texture/sampler registry identities, fallback=None, material index/generation/registry, material publication revision)`. The invocation/access/registry/fallback values are fixed constituents of the automatic local memo namespace; its four variable integers are compared exactly. Material identity/revision alone would be insufficient across calls because program, texture and sampler publication can change independently.

The selected immutable material Version retains its exact per-instance overrides/values, template layout/defaults, template/pipeline owner, expected program identity/revision and texture/sampler identities. Registry resolution and live generations/revisions are fixed by the same publication read scope. Prepare reads no per-entity transform, hierarchy, visibility, pass/target override, current GL binding or ambient callback. MeshRenderer provides only the material handle to Prepare. The reused version vector includes material publication, template version, authored material revision, program version and every ordered texture/sampler version. Successful copies retain the same resource Versions and independent small value vectors; failures are never memoized. The proof and source hashes were recorded before Step 2 in `logs/rendering/phase65/resource-preparation/semantic-key-proof.md`, `semantic-proof-inputs.json` and `step2-admission.json`.

### Conditional implementation, final result and rollback

The admitted implementation changed only RenderState.cpp: automatic per-call storage of successful prepared binding/version results; exact VersionKey lookup after the original material acquisition; complete copies on a hit; original Prepare/version/error behavior on a miss. No cross-call retention or public API. Entity transforms/hierarchy/bounds/visibility, revision classification, typed errors, old-frame resource ownership, final transactional scene-cache assignment and Physics stayed in their original paths. The rejected exact patch and Release library/executable are preserved under `rejected-candidate/`.

One implementation hypothesis, zero production refinements, one final matched three-pair batch. Predeclared retention required BOTH >=0.100 ms and >=10% whole-render saving in every pair, including all lookup/copy costs. The 0.100 ms condition passed, but every percentage condition failed:

| Series | Fresh control render ms | Trial candidate render ms | Saving ms | Saving | Retention gate |
| --- | --- | --- | --- | --- | --- |
| 1 | 2.27330 | 2.07420 | 0.19910 | 8.758% | FAIL: <10% |
| 2 | 2.28155 | 2.09450 | 0.18705 | 8.198% | FAIL: <10% |
| 3 | 2.29190 | 2.08980 | 0.20210 | 8.818% | FAIL: <10% |

Aggregate render median/p95/p99: fresh control 2.28330/2.66210/3.18090 ms; rejected candidate 2.08355/2.44220/2.97890 ms. Whole-render run-median spread: 0.815%/0.971%. All sampled work/resource counts match exactly: eight draws, 1,476,668 triangles; zero Physics steps, upload calls, buffer allocations, target reallocations, shadow executions, picking passes or readbacks. Maximum image difference 0.002297%, below the unchanged 0.5% limit.

| Common CPU metric | Fresh control median/p95/p99 ms | Rejected candidate median/p95/p99 ms | Existing common comparison |
| --- | --- | --- | --- |
| frame_ns | 16.79435/17.79470/18.45930 | 16.98955/18.09250/18.35580 | WITHIN_THRESHOLD |
| work_ns | 2.36100/2.73900/3.24940 | 2.15930/2.54460/3.11160 | WITHIN_THRESHOLD |
| input_ns | 0.01670/0.03390/0.04860 | 0.01660/0.03510/0.05730 | NOISY |
| update_ns | 0.05465/0.07890/0.09490 | 0.05340/0.07990/0.11000 | WITHIN_THRESHOLD |
| render_ns | 2.28330/2.66210/3.18090 | 2.08355/2.44220/2.97890 | WITHIN_THRESHOLD |
| physics_ns | 0.00000/0.00000/0.00000 | 0.00000/0.00000/0.00000 | WITHIN_THRESHOLD |
| readback_ns | 0.00000/0.00000/0.00000 | 0.00000/0.00000/0.00000 | WITHIN_THRESHOLD |

The net whole-render benefit is smaller than the instrumented eligible duplicate work; no exact allocation/lookup/copy cause was isolated, and no additional investigation or timing batch was run. The declared stop condition was honored: restore the conditional production change and return the evidence. A remaining optional refinement was not used to bypass the one-final-batch limit.

Returned RenderState.cpp matches the fresh control source exactly. Its already-recorded control render median/p95 is 2.28330/2.66210 ms, work 2.36100/2.73900; this is source-identical control evidence, not a new post-rollback timing run. Historical limits remain render <=1.05755/1.92696 and work <=1.14598/2.11812 ms. Both the control and rejected candidate still fail historical acceptance. `restored-source-historical-comparison.json` is analysis of existing data only.

### Correctness, validation and returned scope

The new real-scene probe uses 36 shared entities and independent uncached Prepare as its oracle. It checks exact parameter bits/layout, pipeline identity, resource identities/revisions/owner pointers and complete typed errors; mixed material overrides, independent replacements, stale/destroyed/reused generations, template/pipeline rebinding, immutable retained old frames, independent per-entity value storage, target/hierarchy/visibility, late scene failure/retry and authoritative Physics/interpolation behavior. Release compares 1,228 full identity-bearing records with the fresh control, including exact transforms/bounds/revisions/errors.

The initial positional comparator failed because UpdateWorldTransforms sorts process-generated UUIDs. Runtime assertions passed and all full records already matched as multisets. The validator now compares complete sorted records with multiplicity, without dropping/normalizing any semantic field. Original failure: `final/Release-state/`; explanation: `comparator-correction.json`; corrected trial: `final/Release-state-v2/`. No production refinement or timing retry was consumed.

| Check | Result / input identity | Evidence under `logs/rendering/phase65/resource-preparation/` |
| --- | --- | --- |
| Fresh repaired-source control Rebuild and semantic/numeric pre-edit gates | PASS | `control-build.json`, `control/manifest.json`, `semantic-key-proof.md`, `step2-admission.json`, `fine-batch/summary.json` |
| Trial candidate full Debug/Release rebuilds, clean instrumented Release PhysicsTests link/no SDL import, full CPU suites | PASS for rejected implementation | `final/Release-build/results.json`, `final/Debug-build/results.json` |
| Trial exact material/transform/revision/error/retention/Physics oracles and normal header boundary | PASS for rejected implementation | `control-probe/results.json`, `final/Release-state-v2/results.json`, `final/Debug-state/results.json` |
| Trial submission/shadow/cache/depth/color/retention/cross-pass and four app smokes per configuration | PASS for rejected implementation | `final/Release-submission/results.json`, `final/Debug-submission/results.json` |
| One final three-pair comparison | FAIL retention gate (<10% every pair); stable whole-render medians, work/image checks PASS | `final/paired-common/summary.json` |
| Restore only new production change and preserve rejected binary/source | COMPLETE | `rollback.json`, `rejected-candidate/manifest.json`, `rejected-candidate/RenderState.patch` |
| Returned source: full instrumented Release Rebuild All, separate clean PhysicsTests link/no SDL import, full CPU/grouping | PASS | `restored/Release-build/results.json`, build and clean-link binlogs |
| Returned source: affected Debug consumer build, semantic/boundary/submission/cross-pass/retention checks and four app smokes per configuration | PASS | `restored/Debug-submission/results.json`, `restored/Debug-state/results.json`, `restored/Release-state-v2/results.json`, `restored/Release-submission/results.json` |
| Exact scope, restored source, protected/index/history and historical thresholds | PASS after final packet audit | `packet-verification.json`, `evidence-manifest.json` |


The rejected trial's full CPU suites passed 18,984 Release and 18,814 Debug checks. Returned source was rebuilt and revalidated as listed; Debug used affected incremental builds, and the previously accepted full Debug CPU evidence for the identical restored production inputs is retained. `validate_candidate.py` and `validate_restored.py` record exact commands/macro environments. Reproduce clean Release closure with `python tools/test_frame_submission.py --configuration Release --timing-baseline --build-closure-only --output <fresh-dir>`.

Retained new working-tree changes are only `tools/render_state_probe.cpp`, `tools/test_render_state.py`, this appended report and permitted local proposal/review/cache/evidence metadata. No new production change remains. Prior FrameSubmission.cpp and accepted counter/context repair bytes, canonical RigidBodySimulation source, 45 protected paths, index/history and Phase 13 reference remain unchanged.

GPU/UI uncertainty, historical timing noise, async texture/mesh stalls, shadow-culling tails and remaining extraction-scaling limitations remain OPEN. Earlier GPU/capture/async/scaling evidence retains its original input identity; no new batch or acceptance is claimed. No seal, approval, commit, tag, push or Phase 66 work.
