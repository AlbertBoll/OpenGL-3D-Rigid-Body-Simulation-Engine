# BUILD PHASE 00 review: Revision 04

Workflow: **AWAITING HUMAN REVIEW**. Complete Phase 00 acceptance is satisfied; no checkpoint approval or publication has occurred.

B00-RUNTIME-010 is dispositioned as an NVIDIA OpenGL resource-exhaustion fast-fail caused by overlapping graphics validation probes on the current machine. The actual failing process was captured: its NVIDIA worker executes FAST_FAIL_FATAL_APP_EXIT, and the driver's stack message explicitly reports GPU memory exhaustion for that process/thread. Revision 04 corrects only validation scheduling. Four fresh serialized Release probes pass. All accepted application/engine corrections, build inputs, dependencies and protected resources remain unchanged.

Contract: [BUILD_PHASE_00.md](../phases/BUILD_PHASE_00.md). Human authorization: `REVISE BUILD PHASE 00`, Revision 04, limited to B00-RUNTIME-010 investigation/resolution and subsequent final acceptance confirmation. The user explicitly accepts Revision 03 Editor corrections and all earlier corrections. No Phase 01, CMake, Conan, dependency/toolchain/CRT/target-graph/driver change, unrelated behavior change or Git publication is authorized or performed.

## Immutable lineage and scope

Revision 03 remains valid historical evidence, preserved byte for byte as [revision-03-review.md](../evidence/phase-00/revision-04/revision-03-review.md) and [revision-03-snapshot.json](../evidence/phase-00/revision-04/revision-03-snapshot.json). Its snapshot SHA-256 is `b7555343afd09764aa9e151cff09ad8bba47f028a4fc9fb96f8cab64e35d8418`. The local archive is `.codex/build/phase-00-revision-03-snapshot.json`. Its active seal was invalidated only after the archive and Revision 04 entry were frozen.

Initial seal `86621b4bcbcbd46a79eaacc35de270b0b833b6eda9b1138a503b2b1eeb1f2a70`, Revision 01 seal `f2094ad1bbddaa7cde782adcf2a1f0a28bd5249cd55dc8897589ef73b797230e`, and Revision 02 seal `42e372560f4f0c2b650ba99ad735743a0c57e5fac3ac11aeadec89af74440885` remain in the existing immutable chain. Earlier reviews, registries, retained helpers, dumps and raw captures are checked against Revision 04 entry hashes. B00-RUNTIME-001 remains the historical missing-TBB failure resolved by accepted Revision 01. B00-VALIDATION-002 remains the original incomplete timeout evidence, never a Physics failure. B00-VALIDATION-003/006 and B00-RUNTIME-004/005/007/008/009 retain their accepted corrections and validation.

New evidence is separate under `docs/build/evidence/phase-00/revision-04/`; raw logs, retained helper versions, generated layouts and dumps are under ignored `bin-int/build-phase-00-revision-04/`. [lineage.json](../evidence/phase-00/revision-04/lineage.json) records exact new capture ownership before execution. Reserved diagnostic filenames that were not needed after reproduction are not checkpoint members. The original `.codex/build/phase-00-entry.json` and all prior entry/ownership receipts are unchanged.

The only new executable tooling is `scripts/build_validation/revise_phase00_r04.py`, `native_debug_phase00_r04.py` and `seal_phase00_r04.py`. Previous helpers remain unchanged. Production source has **no Revision 04 edit**: the complete six-file tracked diff remains the accepted 18 insertions and 12 deletions.

## Demonstrated failure and bounded correction

[The detailed classification](../evidence/phase-00/revision-04/classification.md) distinguishes application, shared runtime, TBB, SDL/OpenGL/driver, concurrent pressure, harness and unresolved-intermittency alternatives.

The original mandatory [Revision 03 capture](../evidence/phase-00/revision-03/commands/startup-Release-RayTracing.json) remains **FAIL**: `3221226505 / 0xc0000409`, 5.704 seconds, before any close request, no forced termination. Original raw SHA-256 `5749750fe228b318cc77d135962ad6d8aaaf35d3943c08f9a02b916f02fe2470`; compressed SHA-256 `a547ddd8679ce31a2a347080505cf0608869bf01b678da7e51e9033ad5dd7efe`. Neither its classification nor its bytes are overwritten. Later passes alone were not a waiver.

| Ordered investigation | Result and implication |
| --- | --- |
| Six isolated ordinary Release RayTracing probes | 6 PASS; unchanged executable, source CWD, layout, staged runtime, restricted PATH, 60-second observation and normal close |
| Separate recorded application startup overlap | RayTracing first, Breakout about +219 ms, RigidBodySimulation about +363 ms. First round PASS; second RayTracing launch reproduces 0xc0000409 at 3.375 seconds; companions PASS |
| Comparison and GPU sampling | About 3944 MiB during sampled isolated runs; ordinary overlap reaches 7961 MiB on the 8192 MiB GPU. Failed allocation peak itself is not measured by the two-second sampler |
| CDB normal-heap overlap | Six PASS; retained as timing-sensitive diagnostics, not a waiver |
| Native external debug-event overlap | Four rounds, two captured fast-fails and two clean runs. Normal heap, no application breakpoint, injected code or upfront symbol loading |
| Actual failing process dump | Native round 04: PID 45276, TID 9280; fatal event at 4.672 seconds; full-memory dump 4,137,440,798 bytes |
| Offline exception/thread/module analysis | Second-chance 0xc0000409, flags 1, parameter 7; RIP `00007ffc00baee7c`, `nvoglv64.dll+0xFBEE7C`, `int 29h`; all 48 thread stacks and loaded module identities retained |
| Driver message recovered from that dump | NVIDIA worker stack contains GPU-memory exhaustion error 6, naming the captured PID/TID. This directly connects the message and fatal event |
| Historical event correlation | Windows Application event 204268 at 2026-09-13T05:06:15.1352539Z reports the same driver error for raytracing.exe, within the original failure window |

The fatal thread is an NVIDIA worker; its stack does not invoke an application or TBB fatal-exit path. TBB workers are waiting in the captured dump. The immediate mechanism is a driver-requested fatal exit under graphics resource pressure, not evidence of a stack-buffer overflow. The source's existing large shadow allocations explain substantial per-process GPU demand; no allocation, ownership, threading or rendering design is changed.

The controlled app-only overlap reproduces the signature without the historical Editor build or Physics workloads, so repeating those accepted corrections/builds is unnecessary. The original scheduler state, full inherited environment and GPU occupancy were not recorded and cannot be recreated exactly. [historical-overlap.json](../evidence/phase-00/revision-04/historical-overlap.json) preserves every recorded overlapping command interval, including preceding Debug resource-probe teardown. The app-only experiment is identified as a controlled subset, not an exact recreation of unrecorded system state.

The narrow correction is a serial graphics-acceptance command. It holds a named Windows mutex, rejects existing application/CDB probes, runs each application through normal close and exit, and restores protected layout bytes before the next launch. Intentional overlap is explicitly diagnostic. The observed process failures remain real failures; their cause is unsuitable concurrent scheduling of GPU-heavy acceptance probes on this machine. Acceptance makes no claim that multiple such applications can run together within this GPU budget.

The full dump is retained locally with SHA-256 `35daea8c9191466406bc52834d95f3ee7ab806f031628f8bc8f94efac62be50a`. [diagnostic-artifacts.json](../evidence/phase-00/revision-04/diagnostic-artifacts.json) indexes supplementary raw/compressed evidence. It includes driver events, GPU samples and debugger scripts; the large ignored dump is hash-sealed and excluded from checkpoint membership.

## Final validation and Premake reference comparison

[validation-matrix.json](../evidence/phase-00/revision-04/validation-matrix.json) maps **all 62 mandatory gates** to their exact evidence and retains 49 Revision 04 command records. Every original mandatory gate has a current PASS disposition; none is missing or waived. The original failed launch remains FAIL, with separate fresh acceptance evidence after the demonstrated scheduling correction.

Fresh final envelope: `C:/Python310/python.exe -B scripts/build_validation/revise_phase00_r04.py acceptance`, repository-root CWD. It serially invokes the exact recorded `_startup` child commands. Each Release application uses its own source CWD, `PATH=C:\Windows\System32;C:\Windows`, 60-second observation, WM_CLOSE to SDL windows and 30-second exit allowance. Every fresh probe creates its window, remains alive, exits 0, shows no runtime-error/assertion dialog and requires no forced termination.

| Fresh final capture | Result | Exact command, timing and hashes |
| --- | --- | --- |
| RayTracing before companion probes | PASS | [acceptance-01-RayTracing.json](../evidence/phase-00/revision-04/commands/acceptance-01-RayTracing.json) |
| Breakout | PASS | [acceptance-02-Breakout.json](../evidence/phase-00/revision-04/commands/acceptance-02-Breakout.json) |
| RigidBodySimulation | PASS | [acceptance-03-RigidBodySimulation.json](../evidence/phase-00/revision-04/commands/acceptance-03-RigidBodySimulation.json) |
| RayTracing after companion probes | PASS | [acceptance-04-RayTracing.json](../evidence/phase-00/revision-04/commands/acceptance-04-RayTracing.json) |

The minimum remaining checks are documentary/integrity checks. The complete **4,296-path Revision 03 behavior/build closure** is verified unchanged, including every application executable and accepted source/resource/runtime/toolchain input. Under the execution skill, validation can be reused when this closure is unchanged. No generation, compilation, tests, benchmarks, resource tracing or staging safeguard runs are falsely claimed as fresh Revision 04 measurements.

| Complete acceptance area | Verified evidence retained |
| --- | --- |
| Premake generation, normal/profile restoration and all eight targets in Debug/Release | Revision 03 PASS; normal generation remains restored |
| Actual CL language/CRT/source coverage | Revision 03 PASS: C++20, Debug /MTd, Release /MT; GLAD C /TC; 133 normal target/TU pairs per configuration |
| Full PhysicsTests | Revision 03 PASS: Debug 18,726 checks in 193.219 seconds; Release 18,724 checks in 12.406 seconds |
| Focused scene/CRT, transform, scheduling, scaling and manifold cases | Revision 03 PASS in both configurations; normal and profiling telemetry contracts retained |
| Fixed-input normal/profiling and steady-state benchmarks | Revision 03 PASS, matching functional fingerprints and expected telemetry; not a claim about performance overhead |
| Editor early/late close regressions | Accepted Revision 03 final Debug/Release PASS, unchanged source and binaries |
| Four applications in both configurations | Prior valid PASS evidence reused; Release RayTracing now has fresh PASS under corrected scheduling, plus fresh companion controls |
| DLL imports, observed resources and source CWD | Prior valid PASS evidence reused; fresh startup module/runtime identities unchanged |
| Disposable copied-output runtime safeguards | Revision 03 PASS reused; accepted source/output identity and helpers unchanged |
| Final complete input/history/index-entry/whitespace checks | Recorded in integrity.json, git-final.json and final local seal |

## Target, dependency and warning status

The same eight Premake targets, TU ownership, dependencies and target graph remain. Windows x64/MSVC C++20 and static CRT policy are unchanged. Tools remain VS2022 MSBuild 17.14, v143 MSVC 14.44.35207 HostX86/x64 and Windows SDK 10.0.26100.0. No CMake/Conan files, profiles, locks or new toolchain were introduced. Python only orchestrates validation.

Accepted oneTBB 2021.5.0 desktop x64 provenance and import-library match remain unchanged. `external/tbb/lib/tbb12.lib` SHA-256 is `bb72fee5dfebcd6d17448751647a5027027ca4d9309fdae0d4b099c72873ba72`; both staged RayTracing DLLs remain `3bda7c5458d7f43bcd49f0dead5114eac1ea14f573aafc736f833089b1d2cd79`. No staging write was needed. NVIDIA driver stays version 552.44; nvoglv64.dll stays version 31.0.15.5244 and SHA-256 `6fc9e40cca16f29cef7b64001956b389c0ee620aa5585bbb1d963f4185ac1084`.

No compilation occurs in Revision 04; fresh compiler/linker warning counts are **not applicable**. Verified prior counts remain:

| Revision 03 capture | First-party compiler | Linker | Vendor |
| --- | --- | --- | --- |
| Normal Debug / Release | 0 / 0 | 0 / 0 | 986 / 0 |
| Profile Debug / Release | 0 / 0 | 0 / 0 | 174 / 0 |
| Final Editor Debug / Release | 0 / 0 | 0 / 0 | 58 / 0 |

The known vendor diagnostic context and first-party instantiation locations remain in the compiler audit. No suppression, warning-policy change or functional baseline exception is introduced.

## Limits and protected-work integrity

The native first dump attempt failed because the capture helper initially used the wrong packing for MINIDUMP_EXCEPTION_INFORMATION. Its original helper, empty dump, exception/register/module record and FAIL metadata remain unchanged; the corrected helper produced the valid round-04 dump. Dump writing pauses the fatal process and extends elapsed capture time to 45.75 seconds; the fatal event time is separately recorded. The earlier ordinary reproduced failure has no retrospectively obtainable stack. CDB repeats passing did not override it.

Release first-party PDBs and NVIDIA private symbols are unavailable. All thread stacks retain module-relative addresses; the driver is identified by its loaded image and hash, not by treating a distant exported symbol as a private function name. Optional !peb/!address type queries fail without OS symbols and are reported unavailable. Precise internal driver allocation/budgeting remains opaque. NVIDIA's general error documentation also mentions command-stream limits; the local classification rests on the actual failing dump plus controlled concurrency/pressure evidence. No claim excludes every possible independent application or driver defect.

All entry source/resources/runtime files and historical evidence are preserved. Every tracked or ignored application layout touched by a probe is guarded and restored. `GEngineEditor/imgui.ini` remains `d6facac12733ba8dd41ace25b4f972ece052128c3cf9afce933a46fdec266eb7`, Git blob `12330310ae6e3a2040a89c7188ac29af3498e98d`; the Editor is not relaunched in Revision 04. All 85 tracked runtime files and both accepted staged TBB DLLs are preserved.

Index **path/mode/stage/blob entries** remain identical to entry and the staged diff is empty. The index file hash changed from entry `5772c23095ce2ab450fc3aad23d054f440cbc4d96c3bb7fad80b60ac035a3d30` to an intermediate `d50badfcba6e9b27b8a77003cbbc3ebc815c9a42544aa49305cc0e4a4246582c`, and changed again during sealing. This is consistent with index cache refresh; the exact writer and original cache bytes are unavailable. The frozen entry was not rewritten and no index restoration or staging was performed. Final observed index hashes are in integrity.json and git-final.json. Raw file identities are reported as observations; the invariant is the complete unchanged logical checkpoint entries, rechecked with GIT_OPTIONAL_LOCKS=0.

## Final Validation Snapshot

Phase 00 / Revision 04; UTC 2026-09-13; AWAITING HUMAN REVIEW. Branch `build/cmake-conan-migration`; validated HEAD and source/previous-checkpoint peeled commit `b3b043b15ee814133e8419466a1bf3a985a3c958`; tag `render-refactor-phase-00-approved`. Configured origin is `https://github.com/AlbertBoll/OpenGL-3D-Rigid-Body-Simulation-Engine.git`. The previously verified source remote refs remain recorded; no publication occurred. Approved Build reference identity: **NONE; human approval pending**.

[candidate-inventory.json](../evidence/phase-00/revision-04/candidate-inventory.json) lists every intended checkpoint path, classification, entry existence, operation, previous blob, raw SHA-256 and expected Git blob. Review/inventory/Git-report final hashes are deferred only to the local seal to avoid recursive hashing. [integrity.json](../evidence/phase-00/revision-04/integrity.json) includes the complete inherited and expanded input closure, old/new helper/tool/SDK identities, observed modules, dumps, resources, generated files and exact protected-history comparisons. [git-final.json](../evidence/phase-00/revision-04/git-final.json) retains expanded untracked/ignored status, logical index entries and literal checks.

| Tracking class | Exact membership source / operation / checkpoint rule |
| --- | --- |
| PHASE_OWNED_TRACKED | Six accepted source corrections enumerated in inventory; no Revision 04 source edit; exact reviewed changes included |
| PHASE_OWNED_NEW_TRACKED | Adopted bootstrap/prior evidence plus separately enumerated Revision 04 helpers, archives, evidence and review; exact bytes included |
| LOCAL_GOVERNANCE | AGENTS, MANIFEST, .codex skills/entry/ownership/seals/receipts and BUILD_STATE; excluded |
| PROTECTED_UNRELATED | Other source/resources/runtime inputs, ignored generated outputs/layout captures/dumps, and untracked staged TBB DLLs; preserved and excluded |

Literal final status:

```text
 M Breakout/src/BreakoutApp.cpp
 M GEngine/src/Core/BaseApp.cpp
 M GEngine/src/Managers/AssetsManager.cpp
 M GEngineEditor/src/SceneApp.cpp
 M PhysicsBenchmark/src/main.cpp
 M PhysicsTests/src/main.cpp
?? bin/Debug/RayTracing/tbb12.dll
?? bin/Release/RayTracing/tbb12.dll
?? docs/build/
?? scripts/
```

`git diff --check`: empty output, exit 0. New candidate text receives exact-file `git diff --no-index --check -- NUL <file>` checks; empty output with exit 0/1 for additions is required. Raw/compressed historical captures are preserved without whitespace normalization. The seal records exact check results and the finalized review hash.

Context expansion was limited to the original launch/helper/concurrency evidence; event/GPU evidence; read-only RayTracing, image-upload and shared graphics allocation context; SDK/debugger structures and captured runtime state; and complete prior/final validation integrity. Each concrete reason and limitation is enumerated in [investigation.json](../evidence/phase-00/revision-04/investigation.json).

Seal: `.codex/build/phase-00-snapshot.json`, finalized after this review. BUILD_STATE, the active seal and future local transaction receipts are mutable control exclusions; no build/behavior input is exempt. Any subsequent build/behavior edit invalidates affected validation. Any candidate/review edit invalidates this approval seal and requires REVISE.

## Human review and checkpoint boundary

- [x] Revision 04 scope is limited to the demonstrated startup failure and final acceptance.
- [x] All accepted corrections and Initial/Revision 01/02/03 evidence remain preserved.
- [x] Original and reproduced failures are retained; actual fatal process evidence supports the scheduling correction.
- [x] Complete mandatory acceptance, language/CRT/warnings, imports/resources and safeguards have valid mapped evidence.
- [x] Exact ownership, protected inputs, index entries, raw/Git hashes and final closure are recorded.
- [x] No stage for checkpoint, commit, tag, push, approval or BUILD PHASE 01 occurred.

Human review may now consider the sealed candidate. This is **not approval**. A later APPROVE command must follow the approval skill and create/publish the exact reviewed checkpoint through its required gates. Rejection requires the rejection skill and verified isolated ownership: restore only owned tracked changes from the source checkpoint and remove only verified phase-created files absent at entry, preserving bootstrap-at-entry files, unrelated work and immutable lineage. No automatic approval, rollback or successor phase follows this review.
