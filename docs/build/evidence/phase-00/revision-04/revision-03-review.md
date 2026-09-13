# BUILD PHASE 00 review: Revision 03

Workflow: **BLOCKED; human review required; not approvable**.

The Editor shutdown and framebuffer causes are established and corrected. Fresh tests, focused cases, normal/profiling benchmarks, Editor close regressions, resource traces, runtime closure and copied-output safeguards are complete. One acceptance gate remains failed: Release RayTracing fast-failed during startup before any close request. Three later ordinary repeats passed, but the original failure has no established cause and is not waived.

Contract: [BUILD_PHASE_00.md](../phases/BUILD_PHASE_00.md). Human instruction: `REVISE BUILD PHASE 00`, Revision 03, authorizing the Editor blockers and remaining acceptance matrix. This instruction permits the narrow source corrections despite the original reference-only contract. Source/previous approved checkpoint: `render-refactor-phase-00-approved`, peeled commit `b3b043b15ee814133e8419466a1bf3a985a3c958`. No Build checkpoint has been approved.

## Initial BLOCKED evidence and revision lineage

The initial B00-RUNTIME-001 missing-TBB failure and B00-VALIDATION-002 Debug test timeout remain historical facts. The original 120-second timeout remains **INCOMPLETE evidence, not a Physics failure**. Original raw startup/timeout captures, reviews, seals and registries were preserved. Initial seal SHA-256: `86621b4bcbcbd46a79eaacc35de270b0b833b6eda9b1138a503b2b1eeb1f2a70`.

Revision 01's accepted TBB origin, version, architecture, import-library match, runtime identity and staging correction are unchanged. Its review and seal remain archived in Revision 02; seal SHA-256: `f2094ad1bbddaa7cde782adcf2a1f0a28bd5249cd55dc8897589ef73b797230e`. No dependency work was redone, replaced or upgraded.

Revision 02 is preserved byte for byte in [revision-02-review.md](../evidence/phase-00/revision-03/revision-02-review.md), SHA-256 `d607e1ee91ee806974c39fce8c8bae012cbaba6efae429c9ba055cc8741f59b5`, and [revision-02-snapshot.json](../evidence/phase-00/revision-03/revision-02-snapshot.json), SHA-256 `42e372560f4f0c2b650ba99ad735743a0c57e5fac3ac11aeadec89af74440885`. Its four accepted source corrections for B00-VALIDATION-003, B00-RUNTIME-004, B00-RUNTIME-005 and B00-VALIDATION-006 are unchanged.

Revision 03 evidence is separate under `docs/build/evidence/phase-00/revision-03/`. Raw debugger logs, command files, dumps, generated layouts, retained capture-helper versions and the disposable fixture are under ignored `bin-int/build-phase-00-revision-03/`. [lineage.json](../evidence/phase-00/revision-03/lineage.json) records entries, seals, authorization and exact ownership receipts. Earlier canonical raw captures and all 168 retained Revision 01/02 generated-evidence files passed immutable-hash checks.

## Authorized changes and demonstrated causes

[root-causes.md](../evidence/phase-00/revision-03/root-causes.md) records source ownership, destruction order and independent live evidence established before each change.

| Issue | Demonstrated cause | Minimum correction |
| --- | --- | --- |
| B00-RUNTIME-007 | EntryPoint deletes through the virtual destructor. SceneApp frees shared managers, then BaseApp repeats those frees. A fresh 120-second close probe reaches a freed Texture through AssetsManager and BaseApp destruction | Remove SceneApp's three redundant shared-manager cleanup calls; preserve BaseApp cleanup and SceneApp audio shutdown |
| B00-RUNTIME-008, earlier framebuffer observation | Two independent early-close captures assert at RenderTarget.cpp:266 through OnResize(1280,720), SceneApp::Update and BaseApp::Run. Live state shows m_Running=false and zero windows after input destroys the GL context | Break the Run loop after ProcessInput clears m_Running, before Update/Render |
| B00-RUNTIME-009, exposed in the same destructor path | SceneApp manually deletes a camera already owned by its scene/rig hierarchy. Live pre-delete m_EditorCamera is 0x000001e6bd183ed0; the later failing unique_ptr delete targets exactly that address | Remove the redundant manual camera delete; preserve destruction by the existing scene/rig owner |

The framebuffer failure is a real shutdown-order defect exposed by a close request queued during initialization, separate from the duplicate deletions. RenderTarget behavior and its assertion are unchanged. The camera correction stays within the requested redundant derived-cleanup path: Actor, CameraRig, Scene and their ownership implementation are unchanged. No cleanup was suppressed, skipped or replaced with a leak.

Revision 03 changes only `GEngine/src/Core/BaseApp.cpp` and `GEngineEditor/src/SceneApp.cpp`: six added and four removed lines. The complete candidate includes the four accepted Revision 02 corrections; across all six tracked files the diff is 18 added and 12 removed lines. Three new helpers capture, audit and seal evidence. Earlier helpers remain unchanged.

## Target, dependency and runtime impact

The same eight Premake targets and all source/header ownership remain intact. No target graph, linkage policy, dependency version, SDK selection, compiler selection, CRT policy, CMake or Conan change occurred. Policy remains Windows x64/MSVC, C++20, Debug /MTd, Release /MT and GLAD C /TC. Tools remain VS2022 MSBuild 17.14, v143 MSVC 14.44.35207 HostX86/x64 and Windows SDK 10.0.26100.0.

Accepted TBB remains oneTBB 2021.5.0 desktop x64. Consumed `external/tbb/lib/tbb12.lib` SHA-256: `bb72fee5dfebcd6d17448751647a5027027ca4d9309fdae0d4b099c72873ba72`. Both staged RayTracing tbb12.dll outputs retain SHA-256 `3bda7c5458d7f43bcd49f0dead5114eac1ea14f573aafc736f833089b1d2cd79`. Matching-distribution proof remains in Revision 01. Fresh [runtime-audit.json](../evidence/phase-00/revision-03/runtime-audit.json) resolves all eight direct/relevant transitive closures with x64 non-system nodes. Windows System32/API-set forwarding is the declared OS boundary; startup captures record observed loaded modules.

The [copied-output safeguard validation](../evidence/phase-00/revision-03/safeguard-results.json) relocates byte-identical accepted helpers and current outputs into a new Revision 03 fixture. All eight original postbuild checks pass. Missing copied SDL/TBB fail as expected; exact TBB staging creates only absent copied outputs; repeat staging performs no write. Wrong source identity, mismatched existing output, protected runtime destination and unauthorized output root are rejected. The original Revision 01 fixture, source resources and all 85 tracked runtime files remain unchanged.

## Fresh validation and reference comparison

[validation-matrix.json](../evidence/phase-00/revision-03/validation-matrix.json) contains all 80 command records and the exact 62 required-command inventory. Each record includes exact argv/command, CWD, configuration, environment overrides, UTC start, elapsed time, timeout, exit/result and raw/gzip SHA-256. All 62 required commands ran: 61 pass and one fails. No required command is NOT RUN. Diagnostics and repeat measurements are separately identified.

| Required check | Debug | Release |
| --- | --- | --- |
| Full normal eight-target rebuild | PASS, 86.047 seconds | PASS, 108.797 seconds |
| Actual CL language/CRT/coverage | PASS, 10 CL calls, 133 target/TU pairs | PASS, 10 CL calls, 133 target/TU pairs |
| Full PhysicsTests, timeout 1,800 seconds | PASS, 18,726 checks, 193.219 seconds | PASS, 18,724 checks, 12.406 seconds |
| Solver-storage/CRT | PASS | PASS |
| Runtime-transform | PASS, 57 checks | PASS, 57 checks |
| Scene-runtime-lifecycle | PASS, 9 checks | PASS, 9 checks |
| Fixed-scheduling | PASS | PASS |
| Absolute-scaling | PASS, 147 checks | PASS, 147 checks |
| Normal box-manifold counter regression | PASS, 1,028 checks | PASS, 1,028 checks |
| Profiling box-manifold counter regression | PASS, 1,028 checks | PASS, 1,028 checks |
| Fixed normal benchmark | PASS, six rows, 10.688 seconds | PASS, six rows, 0.875 seconds |
| Fixed profiling benchmark | PASS, six rows, 5.531 seconds | PASS, six rows, 0.625 seconds |
| Additional normal/profiling steady-state benchmarks | PASS / PASS | PASS / PASS |
| Editor final early-close regression, request at 45 seconds | PASS, clean exit after 118.844 seconds | PASS, clean exit after 86.266 seconds |
| Editor final late-close regression, request at 120 seconds | PASS, clean exit after 121.719 seconds | PASS, clean exit after 121.203 seconds |
| Breakout startup/resources/normal close | PASS | PASS |
| GEngineEditor startup/resources/normal close | PASS | PASS |
| RayTracing startup/resources/normal close | PASS | FAIL once; three ordinary repeats PASS; unresolved |
| RigidBodySimulation startup/resources/normal close | PASS | PASS |
| Direct/relevant transitive imports | PASS, four applications | PASS, four applications |
| Disposable staging/safeguards | PASS | PASS |

Generation uses bundled `vendor/bin/premake/premake5.exe --physics-profiling vs2022`, then `vendor/bin/premake/premake5.exe vs2022` restores normal generation. Builds use the pinned MSBuild executable with `GEngine.sln /m:2 /nr:false /nologo /p:Configuration=<Debug|Release> /p:Platform=x64 /v:diag`; profile uses `/t:Build`, normal uses `/t:Rebuild`. After the final camera correction, `/t:GEngineEditor` recompiles SceneApp and relinks Editor in both configurations. Actual final compiler/link commands are audited. All other executable/archive hashes match the pre-camera-edit record, so their fresh tests, benchmarks and startup evidence remain applicable to final binaries.

Full tests run `bin/<config>/PhysicsTests/PhysicsTests.exe` from the repository root with timeout 1,800 seconds. Focused selectors are `--solver-storage`, `--runtime-transform`, `--scene-runtime-lifecycle`, `--fixed-scheduling`, `--absolute-scaling` and `--box-manifolds`, each with timeout 600 seconds. Both full runs completed; neither reports a Physics failure.

Normal/profiling benchmarks use `--body-counts=50,100,200,500,1000,2000 --warmup=2 --samples=5 --dt=0.008333333`, repository-root CWD and timeout 1,800 seconds. Additional steady-state runs add `--steady-state-warmup-steps=4 --steady-state-measured-steps=8`. Existing validation/determinism checks remain active. Normal counters/timers are zero; profiling reports positive contacts, constraints and world timings in every row. Normal/profile final-state fingerprints match within each configuration. The accepted telemetry assertion passes against both runtime contracts without enabling profiling globally.

Some timings overlap independent validation workloads. These fixed-input functional measurements do not establish profiling overhead or performance differences between configurations; exact capture times and concurrent investigation history remain available in the command records.

Ordinary apps run from their own source directory with `PATH=C:/Windows/System32;C:/Windows`, remain alive for 60 seconds (Editor 120), then receive normal close with a 30-second exit allowance. Successful probes have exit 0, no forced termination and no assertion/runtime-error dialog. Separate Editor debugger probes cover earlier and later close requests.

[resource-audit.json](../evidence/phase-00/revision-03/resource-audit.json) combines ordinary startup logs with independent file-open paths and return handles. All eight traced runs exit normally and all observed repository resource opens succeed. Traces cover fonts, terrain, textures, shaders, models, audio banks, Breakout levels/images and layout inputs as used by each app. Release tracing supplies evidence where logging is compiled out. Successful read-open counts, Debug/Release: Breakout 17/17, Editor 80/81, RayTracing 4/4, RigidBodySimulation 49/49; additional metadata opens are retained. OS/driver compatibility probes remain in raw traces and are distinguished from application resources. Ordinary Debug logs contain no fallback/resource errors. RayTracing Release's aggregate startup/resource gate remains blocked by its original failed ordinary launch.

## Warning status

[compiler-audit.json](../evidence/phase-00/revision-03/compiler-audit.json) retains actual CL/link/archive commands, per-target coverage, compiler/SDK identities and full diagnostic context.

| Build capture | First-party compiler warnings | Linker warnings | Vendor warnings |
| --- | --- | --- | --- |
| Normal Debug, all targets | 0 | 0 | 986 |
| Normal Release, all targets | 0 | 0 | 0 |
| Profile Debug, 118 changed target/TU pairs | 0 | 0 | 174 |
| Profile Release, 118 changed target/TU pairs | 0 | 0 | 0 |
| Final Editor Debug, one TU | 0 | 0 | 58 |
| Final Editor Release, one TU | 0 | 0 | 0 |

Vendor diagnostics retain the known spdlog/fmt/MSVC C4996/STL4043 checked-array-iterator signature. Reported instantiation locations are preserved and audited. No first-party instantiation diagnostic or blanket suppression was introduced. Counts are per capture.

## Remaining blocker and diagnostic limits

**B00-RUNTIME-010: Release RayTracing startup fast-fail, cause unresolved.** [The original capture](../evidence/phase-00/revision-03/commands/startup-Release-RayTracing.json) records exit `3221226505 / 0xc0000409` after 5.704 seconds, before any close request, without forced termination or a diagnostic dialog. Raw SHA-256: `5749750fe228b318cc77d135962ad6d8aaaf35d3943c08f9a02b916f02fe2470`; gzip SHA-256: `a547ddd8679ce31a2a347080505cf0608869bf01b678da7e51e9033ad5dd7efe`.

That launch overlapped other applications' startup probes. Three later ordinary 60-second repeats, a resource-traced run and a debugger run using `_NO_DEBUG_HEAP=1` exit normally. Concurrency, timing, application/runtime and driver causes have not been distinguished. Those passes do not establish a probe artifact or erase the failure. No failing stack was obtained in debugger repeats. No evidence invalidates accepted TBB or Editor corrections, and neither was changed for this failure.

The smallest additional preparation contract is scoped Release RayTracing fast-fail investigation: obtain a failing dump/stack with the documented source CWD and protected inputs, distinguish application/runtime/driver or probe-concurrency cause, and separately authorize only the demonstrated minimum correction if required. No RayTracing source, dependency, driver, threading or rendering change is made here. Broader architecture/migration work remains outside scope.

All diagnostic limitations remain in their original records. The first Editor debugger launch was sandbox-denied before app launch; an elevated repeat captured the assertion. A minidump lacked heap fields; a subsequent live capture established running/window state. The first corrected Editor diagnostic captured the camera AV stack but stopped on an invalid frame-specific expression before its optional dump; the independent ownership capture saved the dump. The first Breakout trace parsed CDB's clean-exit phrase incorrectly; a separate repeat passes. Initial Release profile samples overlapped final build seconds; verified post-build repeats are authoritative. A RayTracing diagnostic waited at process termination until its harness stopped it; the normal-heap diagnostic's FAIL capture status means the sought dump was absent despite app exit 0. No raw record is overwritten or silently relabeled.

[baseline-registry.json](../evidence/phase-00/revision-03/baseline-registry.json) preserves every initial/revision blocker and its disposition. The [execution skill](../../../.codex/skills/build-phase-execution/SKILL.md) requires: **"Failed mandatory validation ends BLOCKED."** The unresolved startup gate prevents acceptance; no historical or repeated pass is used as a waiver.

## Protected-input and Git integrity

Every Editor probe starts with verified tracked `GEngineEditor/imgui.ini` bytes and restores them afterward. Generated layouts remain in separately named evidence files. Restored SHA-256 is always `d6facac12733ba8dd41ace25b4f972ece052128c3cf9afce933a46fdec266eb7`, checkpoint blob `12330310ae6e3a2040a89c7188ac29af3498e98d`. Runtime-generated layout changes are excluded from checkpoint membership. Other tracked app layouts receive the same guard where present. No resource contents or source-tree aliases changed.

Integrity verifies 1,785 protected/history records, all 85 tracked runtime files and all 168 retained prior generated-evidence files. Accepted Revision 02 corrections are unchanged within Revision 03. Of 1,568 original tracked files, 1,562 remain unchanged from the source checkpoint; only the six owned corrections differ.

The index is byte-identical to Revision 03 entry, SHA-256 `553d70f403484f03a259beea4ac9df94b5f97186a6d855463b0a13371c9a093c`. Its logical staged entries also match Revision 02. Earlier seals retain their own index-byte identities. No staging occurred. Literal status:

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

`git diff --check`: empty output, exit 0. New candidate text receives exact-file `git diff --no-index --check -- NUL <file>` checks; empty output with exit 0/1 is accepted for additions. Historical raw text remains unchanged. [git-final.json](../evidence/phase-00/revision-03/git-final.json) and the seal retain literal results, index entries, expanded status and hashes.

## Final Validation Snapshot

Phase 00 / Revision 03; validation dated 2026-09-13 UTC; BLOCKED. HEAD `b3b043b15ee814133e8419466a1bf3a985a3c958`, branch `build/cmake-conan-migration`, origin `https://github.com/AlbertBoll/OpenGL-3D-Rigid-Body-Simulation-Engine.git`. Source/previous approved tag and peeled commit remain as stated above. Prior remote verification is retained; no publication occurred. Approved reference identity: **NONE; human review pending**.

The exact candidate contains **402 files** in [candidate-inventory.json](../evidence/phase-00/revision-03/candidate-inventory.json): adopted bootstrap/evidence/review files, six tracked corrections and separately enumerated Revision 03 evidence/helpers. Every path has classification, entry existence, operation, previous blob, raw SHA-256, expected Git blob and checkpoint membership. Final review/inventory/Git-report hashes are deferred to the local seal to avoid recursive hashing.

| Revision 03 modified path | Class / entry / operation | Final raw SHA-256 | Expected Git blob | Checkpoint |
| --- | --- | --- | --- | --- |
| `GEngine/src/Core/BaseApp.cpp` | PHASE_OWNED_TRACKED / present / modify | `65368aa2914658b166a7be496413755076045a646b7aacdfdf4d0c4831c728d0` | `b899870dd67910adea452a95ae217e89b9220c2b` | Yes |
| `GEngineEditor/src/SceneApp.cpp` | PHASE_OWNED_TRACKED / present / modify | `8edeff7f6abac15d95ca5714aba238f809899a468bee0bf06141750cd0d66fc0` | `f58c9236019372ca98195f0275451432cf51f202` | Yes |

Accepted Revision 02 sources remain PHASE_OWNED_TRACKED with accepted hashes. Other versionable phase artifacts are PHASE_OWNED_NEW_TRACKED. AGENTS, MANIFEST, .codex instructions/entries/seals/receipts and BUILD_STATE are LOCAL_GOVERNANCE and excluded. All other source/resources/runtime files, ignored projects/binaries/logs/layout captures, disposable outputs and both untracked staged TBB DLLs are PROTECTED_UNRELATED and excluded. No wildcard stands for checkpoint membership.

[integrity.json](../evidence/phase-00/revision-03/integrity.json) attaches the **4,296-path behavior/build input closure** and before/after transitions: changed/unchanged source and headers, build/capture helpers, resources, import libraries, accepted runtime origin, DLLs, actual compiler/link reads, SDK/CRT/tool implementations, generated projects/PDBs/binaries/tlogs, observed modules and retained diagnostics. Added/absent inputs are explicit. No Conan profiles/locks or CMake toolchains were introduced. Final camera correction changes only Editor binaries; all other fresh validation binaries remain identical afterward.

Context expansion was limited to SceneApp/BaseApp/EntryPoint and manager destruction; input/event/window/SDL context shutdown and RenderTarget assertion; the same destructor's camera pointer and existing Actor/CameraRig/Scene owner; actual compiler/link/runtime/resource evidence; and read-only RayTracing failure classification. No unrelated roadmap or architecture was adopted.

Seal: `.codex/build/phase-00-snapshot.json`, written after this review and hashing the complete final candidate. Initial and Revision 01/02 seals remain separately archived. BUILD_STATE, active seal and future local transaction receipts are mutable control exclusions; no behavior/build input is exempt. Later behavior/build edits require affected validation again; candidate/review edits invalidate the seal.

## Human review and checkpoint boundary

- [x] Editor corrections have independent cause evidence and exact ownership.
- [x] Earlier reviews, seals, registries, captures and accepted corrections are preserved.
- [x] All mandatory commands ran; failures and diagnostic limits are distinguished.
- [x] Actual language/CRT/warning, imports, resources and safeguards are recorded.
- [x] Protected inputs/index and exact candidate membership are recorded.
- [ ] Complete Phase 00 acceptance passes: B00-RUNTIME-010 remains unresolved.

Human review is requested for this BLOCKED Revision 03 and the proposed narrow RayTracing investigation contract. This is not checkpoint approval. **No stage for checkpoint, commit, tag, push, approval or BUILD PHASE 01 occurred.**

A separate REJECT command must use the rejection skill and exact ownership/entry records: restore only six owned tracked sources from the verified source checkpoint, remove only verified phase-created files absent at entry, preserve bootstrap files that existed at entry and unrelated work, and retain immutable historical evidence/receipts. No broad reset, clean or automatic rollback is prescribed. Approval must wait for complete acceptance and explicit human authorization.
