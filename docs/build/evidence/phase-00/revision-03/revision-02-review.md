# BUILD PHASE 00 review: Revision 02

Workflow: **BLOCKED; human review required; not approvable**.

Revision 02 implements the four authorized corrections. Full normal Debug and Release PhysicsTests and fixed-input normal benchmarks pass. Debug Breakout loads its resources without fallback warnings and closes normally. Resumed validation discovered an additional Editor shutdown failure, `B00-RUNTIME-007`, so the complete Phase 00 acceptance matrix remains incomplete. No failing or missing gate is waived.

Contract: [BUILD_PHASE_00.md](../phases/BUILD_PHASE_00.md). Human instruction: `REVISE BUILD PHASE 00`, Revision 02, limited to B00-VALIDATION-003, B00-RUNTIME-004, B00-RUNTIME-005 and B00-VALIDATION-006. This explicit instruction authorizes the four source corrections despite the original reference-only contract. Source checkpoint: `render-refactor-phase-00-approved`, peeled commit `b3b043b15ee814133e8419466a1bf3a985a3c958`.

## Initial BLOCKED evidence and Revision 01 lineage

The initial missing-TBB failure (`B00-RUNTIME-001`) and 120-second test timeout (`B00-VALIDATION-002`) remain historical facts. The timeout is **INCOMPLETE evidence, not a Physics failure**. Neither original raw capture was overwritten. Initial snapshot SHA-256: `86621b4bcbcbd46a79eaacc35de270b0b833b6eda9b1138a503b2b1eeb1f2a70`.

Revision 01 remains preserved byte for byte in [revision-01-review.md](../evidence/phase-00/revision-02/revision-01-review.md) and [revision-01-snapshot.json](../evidence/phase-00/revision-02/revision-01-snapshot.json). Its original evidence directory and raw logs remain unchanged. Its seal SHA-256 is `f2094ad1bbddaa7cde782adcf2a1f0a28bd5249cd55dc8897589ef73b797230e`; archived review SHA-256 is `3190187742fdce876547f60077f51d7c19271789126c451520a8d92f61ff5055`.

The accepted oneTBB 2021.5.0 x64 distribution, exact import-library provenance, runtime identity, and staging helper were preserved. No dependency work was repeated or broadened. Both staged RayTracing runtime files still hash to `3bda7c5458d7f43bcd49f0dead5114eac1ea14f573aafc736f833089b1d2cd79`. All eight newly rebuilt application import closures resolve. See [runtime-audit.json](../evidence/phase-00/revision-02/runtime-audit.json) and the archived Revision 01 provenance.

Revision 02 evidence is separately located under `docs/build/evidence/phase-00/revision-02/`; retained raw captures and the Editor dump are under ignored `bin-int/build-phase-00-revision-02/`. [lineage.json](../evidence/phase-00/revision-02/lineage.json) identifies the immutable entries, prior seals and exact ownership receipts.

## Authorized revision work and root causes

Root causes were established before the four source edits. [root-causes.md](../evidence/phase-00/revision-02/root-causes.md) records the historical policy, caller-to-loader trace, exception stacks, source locations and limitations. [contract-history capture](../evidence/phase-00/revision-02/commands/contract-history.json) retains the relevant Git history.

| Blocker | Established cause | Minimum correction |
| --- | --- | --- |
| B00-VALIDATION-003 | The face-contact test expects profiling counters in normal builds, although the original profiling contract compiles all counters out and returns an empty snapshot | Test expects four when the linked runtime reports profiling enabled and zero otherwise; physical manifold/contact/resting-state assertions remain |
| B00-RUNTIME-004 | Breakout supplies complete `../Breakout/include/images/<name>.png` paths; AssetsManager adds a shared prefix and a second `.png`, contrary to the original explicit-path API | Pass explicitly marked `./` or `../` relative filenames verbatim; preserve shared-root/extension handling for logical asset names |
| B00-RUNTIME-005 | Debug live stack shows destruction of an already freed texture in BaseApp after Breakout had already freed the same manager resources | Remove the three duplicate manager frees from Breakout; BaseApp still performs shared cleanup once, and application-object destruction remains |
| B00-VALIDATION-006 | Debug live stack reads address `0x50` in spdlog from ShapeBox construction before benchmark logger initialization | Initialize logging before ordinary benchmark ShapeBox construction and apply the existing benchmark client-log-off policy |

The benchmark crash precedes sampling and telemetry. The Breakout crash is independent of whether textures load successfully or contain fallback pixels. Neither requires physics simulation changes. No counter implementation, macro policy, exception handling, resource contents, aliases or duplicate resources changed. Shared cleanup was not skipped: it still runs in BaseApp.

Exact modified tracked files: `PhysicsTests/src/main.cpp`, `PhysicsBenchmark/src/main.cpp`, `GEngine/src/Managers/AssetsManager.cpp`, `Breakout/src/BreakoutApp.cpp`. The diff is 12 added and 8 removed lines. New Revision 02 capture/seal helpers are `scripts/build_validation/revise_phase00_r02.py` and `scripts/build_validation/seal_phase00_r02.py`; earlier helpers are unchanged.

## Additional blocker and scope stop

`B00-RUNTIME-007`: GEngineEditor independently crashes after a later normal close request. Its live stack is `Texture::~Texture -> AssetsManager::FreeTextureResource -> BaseApp::~BaseApp -> SceneApp::~SceneApp`, with freed-heap pointer pattern `0xfeeefeeefeeefef6`. The unchanged `SceneApp.cpp:42-44` also frees managers before BaseApp repeats their cleanup. This is the same defect pattern as Breakout, in an additional caller outside the four explicitly authorized repairs. No Editor source was changed.

The [Editor debugger capture](../evidence/phase-00/revision-02/commands/new-blocker-Editor-Debug-cdb.json) stayed alive without a close request, then captured the access violation after the [separate normal-close command](../evidence/phase-00/revision-02/commands/new-blocker-Editor-close-request.json) at approximately 153 seconds. A minidump and source-resolved live stack were retained. Its capture process exit 0 means diagnostics were captured successfully; it is **not** an application pass.

The earlier [45-second Editor startup probe](../evidence/phase-00/revision-02/commands/startup-Debug-GEngineEditor.json) ended with `0x80000003` and `Framebuffer status error` after a close request during its longer startup. That exact assertion location/root remains unresolved; it is not waived or relabeled as harmless. The later probe demonstrates a distinct reproducible shutdown AV, not the cause of that assertion.

This review stops at the user's authorization to investigate and resolve **only** the four listed blockers. The execution skill also requires: "If broader preparation is required, stop BLOCKED with the smallest proposed new contract." See [.codex/skills/build-phase-execution/SKILL.md](../../../.codex/skills/build-phase-execution/SKILL.md). The smallest proposed additional authorization is Editor shutdown correctness for the duplicate shared cleanup and investigation of the early-close framebuffer assertion, preserving resource contents, rendering behavior, dependencies, toolchain/CRT and target graph, followed by the complete remaining Phase 00 matrix. This is a proposal for human review, not authorization or implementation.

## New validation evidence

All commands use the verified VS2022/MSVC toolchain. App probes use their declared source CWD and `PATH=C:/Windows/System32;C:/Windows`. Hidden SDL windows are identified by class and receive WM_CLOSE; normal acceptance probes do not mask exceptions. Exact argv, command line, CWD, configuration, environment overrides, UTC start, elapsed time, timeout, exit code and raw/gzip SHA-256 are in [validation-matrix.json](../evidence/phase-00/revision-02/validation-matrix.json) and its per-command JSON records.

| Required check | Revision 02 outcome |
| --- | --- |
| Premake normal generation | PASS, bundled `premake5.exe vs2022` |
| All eight targets, Debug and Release | PASS; pinned MSBuild `GEngine.sln /m:2 /nr:false /nologo /p:Configuration=<config> /p:Platform=x64 /v:diag /t:Rebuild` |
| Actual CL language/CRT/TU coverage | PASS; 10 CL invocations and all 133 target/TU pairs per configuration; C++20, /MTd or /MT; GLAD /TC |
| Full Debug PhysicsTests | PASS, 18,726 checks, 197.844 seconds, exit 0; explicitly recorded timeout 1,800 seconds |
| Full Release PhysicsTests | PASS, 18,724 checks, 13.344 seconds, exit 0; timeout 1,800 seconds |
| Fixed normal Debug PhysicsBenchmark | PASS, six body-count rows, 6.344 seconds, exit 0 |
| Fixed normal Release PhysicsBenchmark | PASS, six body-count rows, 0.625 seconds, exit 0 |
| Direct and relevant transitive imports, all four apps in both configurations | PASS, fresh eight-output inventories, no missing runtime imports |
| Debug Breakout source-CWD startup/resources/close | PASS; 46.218-second probe, SDL/2D renderer initialized, zero texture fallback warnings, normal exit 0, no forced termination |
| Debug Editor startup/close | FAIL; earlier framebuffer assertion and later independently captured shutdown AV |
| Remaining six application/config startup/resource cases | NOT RUN after additional scope blocker |
| Focused solver-storage/CRT, runtime-transform, scene-runtime-lifecycle, fixed-scheduling, absolute-scaling in both configs | NOT RUN in Revision 02 after scope stop; Revision 01 ten passes remain historical only |
| Profiling-enabled benchmarks and explicit enabled-counter regression | NOT RUN after scope stop; profiling was never generated, so normal generation remains intact |
| Additional reserved steady-state benchmarks | NOT RUN; not claimed as completed mandatory evidence |
| Disposable copied-output safeguard rerun | NOT RUN after scope stop; accepted Revision 01 safeguard evidence preserved, not claimed as fresh |
| Protected bytes, prior raw captures and index | PASS at final integrity check; transient Editor layout write handled explicitly below |
| Complete Phase 00 acceptance | BLOCKED |

Normal benchmark arguments in both configurations were `--body-counts=50,100,200,500,1000,2000 --warmup=2 --samples=5 --dt=0.008333333`, CWD repository root, timeout 1,800 seconds. Benchmark validation and determinism checks remained active. No global profiling enablement was used to obtain the normal test pass.

The first debugger attempts lacked usable stdout. Separate retries retained full live exception records, source-resolved stacks, registers and disassembly. Their optional dump commands failed due to CDB path escaping, so those capture exits remain FAIL and no baseline dump is claimed. The required stack/location evidence was nevertheless captured live before the edits. The later Editor diagnostic used a corrected dump path. None of these raw records was overwritten or silently reclassified.

## Warning status and target/dependency impact

| Configuration | First-party compiler warnings | Linker warnings | Vendor warnings |
| --- | --- | --- | --- |
| Debug | 0 | 0 | 986 |
| Release | 0 | 0 | 0 |

[compiler-audit.json](../evidence/phase-00/revision-02/compiler-audit.json) retains every actual CL/link/archive command, per-target coverage, SDK/tool identities and diagnostic context. The 986 Debug warnings match the initial vendor-only spdlog/fmt/MSVC C4996/STL4043 signature; first-party instantiation locations are retained and none is reported. No suppression was introduced.

The eight-target Premake graph, target/TU ownership, dependency versions, compiler selection, CRT policy and build scripts remain unchanged. No CMake/Conan or successor-phase work occurred. Both configurations use MSVC 14.44.35207 HostX86/x64 and Windows SDK 10.0.26100.0. The Rebuild operation regenerated build products; all 85 protected tracked runtime files retain their original hashes. No runtime staging wrote protected source/resources/runtime files.

## Protected-input findings

Editor saves `GEngineEditor/imgui.ini` during execution. That file is tracked despite an ignore pattern for untracked ini files. The integrity audit detected its generated change, saved those generated bytes separately, and restored the exact entry bytes from the verified Git blob with its original CRLF representation. Restored SHA-256: `d6facac12733ba8dd41ace25b4f972ece052128c3cf9afce933a46fdec266eb7`. See [protected-layout-restoration.json](../evidence/phase-00/revision-02/protected-layout-restoration.json).

The first Editor probe used the original layout; the later diagnostic used its generated layout. Neither is an accepted Editor runtime result. This transient write is disclosed rather than described as continuous byte preservation. Final protected bytes match entry, and the generated layout is not a checkpoint change. Future Editor validation must explicitly protect this tracked runtime input.

All initial and Revision 01 raw logs, archived reviews/seals, prior helper bytes, dependency/runtime files and protected source/resources were checked against their recorded hashes. The final comparison preserves 1,719 prior candidate/protected records, including 1,564 unchanged tracked files and all 85 tracked runtime files. Four tracked files are owned by this revision. The index remains byte-identical, SHA-256 `6636606189e853606359cd02703c8da0c2b85a45bfc6968e1d405ddec2d82128`.

## Final Validation Snapshot

Phase 00 / Revision 02; BLOCKED; approved reference identity **NONE**. Validated HEAD `b3b043b15ee814133e8419466a1bf3a985a3c958`; branch `build/cmake-conan-migration`; configured origin `https://github.com/AlbertBoll/OpenGL-3D-Rigid-Body-Simulation-Engine.git`. Previous approved source tag/peeled commit are unchanged. The prior remote source-tag verification is retained with its lineage; no publication action occurred.

The exact checkpoint candidate has **224 files**: the prior adopted bootstrap/evidence/review candidate, four owned tracked corrections, and the separately enumerated Revision 02 helper/evidence additions. [candidate-inventory.json](../evidence/phase-00/revision-02/candidate-inventory.json) lists every path and tracking class, entry existence, previous Git blob, raw SHA-256 and expected Git blob. The finalized review, inventory and final Git-report hashes are deferred to the local seal to avoid recursive self-hashing.

| Modified tracked file | Entry existence | Operation | Final raw SHA-256 | Checkpoint |
| --- | --- | --- | --- | --- |
| `Breakout/src/BreakoutApp.cpp` | Present | Modify | `e76a10bb4165a6c893f5894b4ffab9fcb448eaa7085a66bf5df6d6adc56bc043` | Yes |
| `GEngine/src/Managers/AssetsManager.cpp` | Present | Modify | `da1b8cbc4e37da789fab3a8899740e4052cf9810e8f18291e1d56aed05b4fa8a` | Yes |
| `PhysicsBenchmark/src/main.cpp` | Present | Modify | `5862d85b234b5ebefb2601f72be1ecfd5a3cfba902a406da65b6f77573874a6a` | Yes |
| `PhysicsTests/src/main.cpp` | Present | Modify | `0bad6840df0f8d4c18d18287de7d264584cd3e8fb4c3d6f0f83dca40c3520ded` | Yes |

All other versionable phase evidence is `PHASE_OWNED_NEW_TRACKED`. Local AGENTS/MANIFEST/.codex instructions, entries, seals, ownership receipts and BUILD_STATE are `LOCAL_GOVERNANCE` and excluded. Other source/resources/runtime files, ignored generated projects/binaries/logs and both untracked staged TBB DLL outputs are `PROTECTED_UNRELATED` and excluded. The classification does not turn generated outputs into checkpoint inputs.

The conservative behavior/build input closure contains **3,964 paths** in [integrity.json](../evidence/phase-00/revision-02/integrity.json): changed/unchanged sources and headers, all resources/import libraries/runtime files, actual compiler/link read inputs and outputs, tool/SDK/CRT implementation identities, ignored projects/build products, prior runtime origin, observed loaded OS/driver modules, capture helpers and retained raw diagnostic evidence. Exact before/after transitions identify the four source edits and generated rebuild outputs. This is the captured-input closure for a blocked candidate, not a claim that the incomplete runtime/resource acceptance matrix passed.

Literal final `git status --short`:

```text
 M Breakout/src/BreakoutApp.cpp
 M GEngine/src/Managers/AssetsManager.cpp
 M PhysicsBenchmark/src/main.cpp
 M PhysicsTests/src/main.cpp
?? bin/Debug/RayTracing/tbb12.dll
?? bin/Release/RayTracing/tbb12.dll
?? docs/build/
?? scripts/
```

`git diff --check`: empty output, exit 0. New candidate text receives separate `git diff --no-index --check -- NUL <exact-file>` checks; empty output with exit 0/1 is accepted for additions. Original historical raw text is preserved unchanged rather than normalized. Exact checks, literal index entries and status are in [git-final.json](../evidence/phase-00/revision-02/git-final.json); the local seal contains the expanded tracked/untracked/ignored file inventory and final hashes.

Context expansion was limited to the profiling history and counter/test contract; Breakout/AssetsManager/texture-loader path history; the two requested crash stacks and their logger/destructor source; Editor source and debugger stack needed to classify the additional blocker; and actual compiler/SDK/import/runtime evidence for the mandatory audit. No whole roadmap or unrelated phase was loaded.

Local seal: `.codex/build/phase-00-snapshot.json`, written after this review and containing its final hash. The initial and Revision 01 seals are archived separately. The seal itself, BUILD_STATE and future local transaction receipts are mutable control exclusions; no behavior/build input is exempted. Any later source/build-affecting change requires affected validation again; any candidate/review change invalidates this seal.

## Human review and checkpoint boundary

- [x] Four corrections have root-cause evidence and exact source ownership.
- [x] Initial and Revision 01 history, including B00-RUNTIME-001 and B00-VALIDATION-002, remains preserved.
- [x] Actual compiler/CRT/language/warning gates and completed normal tests/benchmarks are recorded.
- [x] Additional Editor failure and transient protected layout write are disclosed.
- [ ] All focused/profiling/application/resource/safeguard acceptance checks complete.
- [ ] Complete BUILD PHASE 00 acceptance passes.

Human review is requested for this BLOCKED revision and the proposed additional Editor scope. This is not an approval request for a checkpoint. **No staging for checkpoint, commit, tag, push, approval or BUILD PHASE 01 has occurred or is authorized by this review.**

If rejected under a separate REJECT command, use the exact ownership/entry records and rejection skill: restore only the four owned tracked source files from the verified source checkpoint, remove only verified phase-created paths absent at entry, preserve bootstrap files that existed at entry and all unrelated work, and retain historical evidence/receipts. No broad reset, clean or rollback command is prescribed here.
