# BUILD PHASE 00 review: revision 01

Workflow: **BLOCKED: acceptance criteria have not passed**

Contract: [Capture the current Premake reference](../phases/BUILD_PHASE_00.md). Human command: `REVISE BUILD PHASE 00`, with explicit authorization for minimum runtime closure and resumed validation. Active phase remains 00; no successor, staging for checkpoint, commit, tag, push or approval.

## Scope and behavior

### Initial BLOCKED evidence (immutable)

The initial review and snapshot are preserved byte for byte in [initial-blocked-review.md](../evidence/phase-00/revision-01/initial-blocked-review.md) and [initial-blocked-snapshot.json](../evidence/phase-00/revision-01/initial-blocked-snapshot.json). Original evidence files under `docs/build/evidence/phase-00/`, including the failed RayTracing launch, the 120-second PhysicsTests timeout, both build logs and the original failure registry, were not rewritten. Initial snapshot SHA-256: `86621b4bcbcbd46a79eaacc35de270b0b833b6eda9b1138a503b2b1eeb1f2a70`.

`B00-RUNTIME-001` retains its historical missing-runtime failure. `B00-VALIDATION-002` retains its historical **incomplete** timeout classification. The new completed failures are separate records; they do not retroactively turn the initial timeout into a Physics failure.

### Authorized revision work

Added `stage_premake_runtime.py` as a narrowly scoped supplement after the existing Premake build. It writes only `bin/Debug/RayTracing/tbb12.dll` and `bin/Release/RayTracing/tbb12.dll`, both absent before this revision. It verifies the upstream archive, exact import-library match, DLL hash and x64 machine type; refuses tracked/unlisted destinations and mismatched pre-existing outputs; and performs no write for an already matching output. The original Premake graph and postbuild safeguards remain byte-unchanged. No source, resource, tracked runtime, compiler policy or dependency version changed.

Replay the authorized output staging with `python scripts/build_validation/stage_premake_runtime.py stage`, then `python scripts/build_validation/stage_premake_runtime.py check`. The helper requires the exact verified upstream distribution retained in ignored `bin-int/build-phase-00-revision-01/tbb-origin`; its origin/hash is documented below. It has no machine-install or alternate-version fallback. The two DLL outputs are excluded from the checkpoint even though Git shows them as untracked.

### New validation evidence and stop

Runtime import closure is established, and Debug RayTracing loads the exact staged DLL, initializes SDL/OpenGL and exits normally in the corrected probe. Resumed validation then established failing normal-suite telemetry assertions, a Debug benchmark access violation, and Breakout resource-path failures plus an access violation after the normal close request.

The specific scope stop is Breakout resource handling: the application passes paths already ending in `.png`; AssetsManager prepends the shared image directory and appends another `.png`. Logs show five malformed paths and 11 checkerboard-fallback warnings. Correcting this needs application/engine path-handling changes or source-tree resource aliases. Your revision explicitly prohibits both behavioral changes and staging into the source resource tree, so no repair or later profiling/app execution was attempted. Already running normal captures were allowed to finish; the isolated staging safeguard and preservation checks were completed.

## Target and dependency impact

The exact library read by both RayTracing link operations is `C:/dev/GEngine-build/external/tbb/lib/tbb12.lib`, SHA-256 `bb72fee5dfebcd6d17448751647a5027027ca4d9309fdae0d4b099c72873ba72`. Link read tlogs also name the original `tbb12_debug.lib`, `tbb.lib` and `tbb_debug.lib`; the latter aliases are byte-identical copies. No library list/order or target changed. Git records the TBB addition at commit `0706a009749651a1b92b57ab1b377a6cba90a979` (2022-05-19).

Provenance is established by byte identity with the [official oneTBB 2021.5.0 Windows distribution](https://github.com/uxlfoundation/oneTBB/releases/tag/v2021.5.0), not a same-named DLL search. Its published archive SHA-256 is `096c004c7079af89fe990bb259d58983b0ee272afa3a7ef0733875bfe09fcd8e`, verified before use. Both desktop and UWP import libraries are byte-identical in that archive; the existing desktop ConsoleApp graph selects the desktop `redist/intel64/vc14/tbb12.dll` from the same distribution.

Selected DLL SHA-256: `3bda7c5458d7f43bcd49f0dead5114eac1ea14f573aafc736f833089b1d2cd79`; PE machine `0x8664`; runtime API version `2021.5`, interface `12050`, matching the repository 2021.5.0/interface-12050 headers. See [tbb-release.json](../evidence/phase-00/revision-01/tbb-release.json), [tbb-package.json](../evidence/phase-00/revision-01/tbb-package.json), and [tbb-import-libraries.json](../evidence/phase-00/revision-01/tbb-import-libraries.json).

All four apps in both configurations have direct import captures and relevant non-system transitive inventories: [runtime-audit-before.json](../evidence/phase-00/revision-01/runtime-audit-before.json) and [runtime-audit-after.json](../evidence/phase-00/revision-01/runtime-audit-after.json). Windows/CRT files are declared by actual paths, machine types and hashes. App launches use only `C:/Windows/System32;C:/Windows` as PATH and each application's actual source directory as CWD. Release Breakout/RigidBodySimulation/RayTracing do not import Assimp after optimization; no extra Assimp copies were needed. The only supplied files are the two TBB outputs.

## Validation and Premake reference comparison

Initial source-tag compiler evidence remains immutable: eight targets, 133 target/TU pairs per configuration, actual C++20 commands, Debug `/MTd`, Release `/MT`, GLAD `/TC`, VS2022 v143 14.44.35207 and SDK 10.0.26100.0. All compilation inputs, generated projects and executables match the initial capture. No new compilation or profiling generation is claimed. Runtime changes were validated by new import/loader/test captures.

| Mandatory check | Revision result |
| --- | --- |
| Initial all-eight-target Debug/Release compiler, CRT and warning evidence | INITIAL PASS; original inputs/executables unchanged; no new build claimed |
| TBB import-library provenance and exact matching runtime | PASS; byte-identical upstream x64 import libraries, publisher-provided archive checksum, PE x64 and runtime API 2021.5/interface 12050 |
| All eight direct and relevant transitive import inventories | PASS; only two missing runtime destinations supplied |
| Output-only staging and disposable safeguard | PASS; missing/wrong DLL and protected-destination rejection; idempotence; original safeguards preserved |
| Full Debug PhysicsTests (1800-second limit) | COMPLETED FAIL: 80/18726; 185.766 seconds |
| Full Release PhysicsTests (1800-second limit) | COMPLETED FAIL: 80/18724; 13.094 seconds |
| Five focused scene/CRT cases per configuration | PASS: solver-storage, runtime-transform, scene-runtime-lifecycle, fixed-scheduling, absolute-scaling |
| Fixed-input normal Debug benchmark | FAIL: 0xc0000005; 32.313 seconds; no stdout |
| Fixed-input normal Release benchmark | PASS: six body counts, fixed warmup/samples/dt; 0.578 seconds |
| Profiling benchmarks and associated builds | NOT RUN after source/resource scope stop; no profiling generation to restore |
| Debug RayTracing source-CWD startup | PASS corrected capture: exact TBB loaded, SDL/OpenGL initialized, clean WM_CLOSE exit |
| Debug Breakout source-CWD startup/resources | FAIL: five malformed texture paths/11 fallback warnings; 0xc0000005 after WM_CLOSE |
| Other Debug apps and all four Release app startups/resources | NOT RUN after new out-of-scope resource blocker |
| Final acceptance | BLOCKED; no failing gate waived |

Full tests used an explicitly recorded 1,800-second limit. Debug completed in 185.766 seconds and Release in 13.094 seconds. Both fail exactly 80 instances of `contact telemetry counts all four generated face contacts and solver constraints`; all other checks pass. Normal `GetPhysicsProfileSnapshot()` returns an empty snapshot when `GE_ENABLE_PHYSICS_PROFILING` is absent, while this test unconditionally expects counters equal to four. No test, counter, macro or normal-generation policy was changed.

Focused cases run in **both** configurations: `--solver-storage`, `--runtime-transform`, `--scene-runtime-lifecycle`, `--fixed-scheduling`, `--absolute-scaling`; all ten pass. The fixed normal benchmark inputs were `--body-counts=50,100,200,500,1000,2000 --warmup=2 --samples=5 --dt=0.008333333`. Release completes; Debug exits `0xc0000005` with empty stdout. Profiling was never generated, so normal generation remains intact and no restoration mutation is required.

The first revision RayTracing probe deliberately hid the SDL window but incorrectly required visibility and failed to send WM_CLOSE to hidden windows. That raw capture is retained as a harness limitation. The separately named corrected capture identifies the SDL window by class, verifies loaded module hashes, and closes normally. Later probes also capture the application's generated GEngine.log inside their compressed raw evidence. No silent overwrite or relabeling of raw captures occurred.

Every exact argv/CWD/config/start/duration/timeout/exit and raw/compressed log hash is retained in [validation-summary.json](../evidence/phase-00/revision-01/validation-summary.json) and individual `commands/*.json` entries listed in the snapshot. Gzip stores verbatim new captures, including whitespace; decompression is checked against retained raw output. Runtime timings are observational, not performance acceptance thresholds.

| Capture | Result / exit | Raw SHA-256 |
| --- | --- | --- |
| [benchmark-Debug-normal](../evidence/phase-00/revision-01/commands/benchmark-Debug-normal.json) | FAIL / 3221225477 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| [benchmark-Release-normal](../evidence/phase-00/revision-01/commands/benchmark-Release-normal.json) | PASS / 0 | `51cdae1a25e5b693aab78adf140115792c32ffdd9491ef3093c1e994510bfc67` |
| [imports-Debug-Breakout](../evidence/phase-00/revision-01/commands/imports-Debug-Breakout.json) | PASS / 0 | `5cbb693e69b6b929f03636125d29d2b0243760ae8c4fc23b64f94c0bf9290939` |
| [imports-Debug-GEngineEditor](../evidence/phase-00/revision-01/commands/imports-Debug-GEngineEditor.json) | PASS / 0 | `d4ef594d201dea9df13f54ad549eb121e6e4fe8d27826d8f3987304216b8aed0` |
| [imports-Debug-RayTracing](../evidence/phase-00/revision-01/commands/imports-Debug-RayTracing.json) | PASS / 0 | `02b221766a7f5c232b7f4d7df8bacf7a730e0d5c7d2f59f143e0927ffebc8d76` |
| [imports-Debug-RigidBodySimulation](../evidence/phase-00/revision-01/commands/imports-Debug-RigidBodySimulation.json) | PASS / 0 | `ff8a06a513723129012bd6829885db6f2502e3f35227a7a038e44ab6507c7a06` |
| [imports-Release-Breakout](../evidence/phase-00/revision-01/commands/imports-Release-Breakout.json) | PASS / 0 | `d977f21ad0590df5eef2ab21d7f5754b83512a6f7f46199957657bd459189bc2` |
| [imports-Release-GEngineEditor](../evidence/phase-00/revision-01/commands/imports-Release-GEngineEditor.json) | PASS / 0 | `2e83dbfc84ca6b38d88b1c228fcabf7dba67703aadd8bc621966ed6656b1b985` |
| [imports-Release-RayTracing](../evidence/phase-00/revision-01/commands/imports-Release-RayTracing.json) | PASS / 0 | `6140a3fd83bcbbc94e7890cb83cc23d5d3656ed065646a254ffa374a34e4f66b` |
| [imports-Release-RigidBodySimulation](../evidence/phase-00/revision-01/commands/imports-Release-RigidBodySimulation.json) | PASS / 0 | `d9ef62d306553e3464d914d7c8d88542507c45a9c85486e0523b51e077b788e6` |
| [runtime-safeguard](../evidence/phase-00/revision-01/commands/runtime-safeguard.json) | PASS / 0 | `035a4e58266876b48588eed76574ffc67e64a319cadd6e5515e65c89f149cae2` |
| [runtime-stage](../evidence/phase-00/revision-01/commands/runtime-stage.json) | PASS / 0 | `8c7047dd6dadd4e348a35ede0722a1ada3565309d79c3eddbedd8d590ad66b02` |
| [startup-Debug-Breakout](../evidence/phase-00/revision-01/commands/startup-Debug-Breakout.json) | FAIL / 1 | `7d27207d916a7c881b1b79bb0a5b3f53607d68539727f86c49acd8ef579f4beb` |
| [startup-Debug-RayTracing-recheck](../evidence/phase-00/revision-01/commands/startup-Debug-RayTracing-recheck.json) | PASS / 0 | `5b440330e5af9ce563ff77921dc9c403a87250bc3602badcb22349dd92b3cfdb` |
| [startup-Debug-RayTracing](../evidence/phase-00/revision-01/commands/startup-Debug-RayTracing.json) | FAIL / 1 | `e4b64db767300d9af21962086589a34474b8738aa5b6963771c2438a94766b05` |
| [tbb-provenance](../evidence/phase-00/revision-01/commands/tbb-provenance.json) | PASS / 0 | `b76e13ef90fd8700a51f0f80166bddf161df72c0e3ed12bb13abbaf32fa373ca` |
| [tbb-runtime-identity](../evidence/phase-00/revision-01/commands/tbb-runtime-identity.json) | PASS / 0 | `27dfceba403e3caf305042b2cdf84ee47486e4bc7771b483609e8fad9c4209a4` |
| [tests-Debug-absolute-scaling](../evidence/phase-00/revision-01/commands/tests-Debug-absolute-scaling.json) | PASS / 0 | `c02bfc2242044134c977862bcfd850b8559c7de8313a33ae480ad9ed8b4af8ba` |
| [tests-Debug-fixed-scheduling](../evidence/phase-00/revision-01/commands/tests-Debug-fixed-scheduling.json) | PASS / 0 | `7c063602b1f88722c214916c2fd8a24720a1bb7cfb856faca6582b2ac49c8b7a` |
| [tests-Debug-full](../evidence/phase-00/revision-01/commands/tests-Debug-full.json) | FAIL / 1 | `a458cd3c8d991729e087bd17fd44cc5aea7d4f16a77a31e6f7f0a4967ed61213` |
| [tests-Debug-runtime-transform](../evidence/phase-00/revision-01/commands/tests-Debug-runtime-transform.json) | PASS / 0 | `699d241f8c73c05a307f8c1744de888822559d372e694aa40ec2a6584265ebf0` |
| [tests-Debug-scene-runtime-lifecycle](../evidence/phase-00/revision-01/commands/tests-Debug-scene-runtime-lifecycle.json) | PASS / 0 | `ec03b708d24709675d917358253b6a16cffc82caa9bb8a86427b9d722752316d` |
| [tests-Debug-solver-storage](../evidence/phase-00/revision-01/commands/tests-Debug-solver-storage.json) | PASS / 0 | `e0660f029fe9265d4790a8f0f1fc7ffe9ecac34c44c5cd1eb9f66ea07b8222a4` |
| [tests-Release-absolute-scaling](../evidence/phase-00/revision-01/commands/tests-Release-absolute-scaling.json) | PASS / 0 | `00e8d57f4b2b1c82aba8af6882c396e97cae0c9c96fbd300fafda95a425282ac` |
| [tests-Release-fixed-scheduling](../evidence/phase-00/revision-01/commands/tests-Release-fixed-scheduling.json) | PASS / 0 | `5ed947a72142c2ca7e2a447a6b9c512209e6a5b28514593be3b094d4ccfc51d9` |
| [tests-Release-full](../evidence/phase-00/revision-01/commands/tests-Release-full.json) | FAIL / 1 | `5406df187d629f15737b0d7c8318766107dbbd5b54825009d8267c5aeaff4e97` |
| [tests-Release-runtime-transform](../evidence/phase-00/revision-01/commands/tests-Release-runtime-transform.json) | PASS / 0 | `9f40af5e9cbfefe3fd062b4b176af4a60edb200925cfa19881fc759b81259248` |
| [tests-Release-scene-runtime-lifecycle](../evidence/phase-00/revision-01/commands/tests-Release-scene-runtime-lifecycle.json) | PASS / 0 | `ec03b708d24709675d917358253b6a16cffc82caa9bb8a86427b9d722752316d` |
| [tests-Release-solver-storage](../evidence/phase-00/revision-01/commands/tests-Release-solver-storage.json) | PASS / 0 | `f9f410ad80657fad045cb31a07e484b94d6eb606966d632155232218b2f1c1d4` |

## Warning status and baseline-known failures

| Evidence | First-party compiler warnings | Linker warnings | Vendor warnings |
| --- | --- | --- | --- |
| Initial Debug compilation, unchanged inputs | 0 | 0 | 986 C4996/STL4043 |
| Initial Release compilation, unchanged inputs | 0 | 0 | 0 |
| Revision runtime-only code/evidence | Not applicable: no compilation run | Not applicable | Not applicable |

Original warning classification and external instantiation contexts remain in the original compiler audit. No suppressions or language/CRT changes were added. Runtime texture warnings are separate from compiler warning gates.

The full lineage registry is [baseline-registry.json](../evidence/phase-00/revision-01/baseline-registry.json). `B00-RUNTIME-001` is resolved for the demonstrated missing dependency; the original failure remains recorded. `B00-VALIDATION-002` remains historical incomplete evidence. New records: `B00-VALIDATION-003` (80 telemetry assertions/config), `B00-RUNTIME-004` (Breakout texture paths), `B00-RUNTIME-005` (Breakout close access violation), and `B00-VALIDATION-006` (Debug normal benchmark access violation). Crash causes are unproven; no failing gate is grandfathered or approved.

Resource evidence and limits are in [resource-audit.json](../evidence/phase-00/revision-01/resource-audit.json). Debug RayTracing log establishes SDL 2.0.20, NVIDIA OpenGL 4.6, font/scene/renderer/camera initialization. Breakout's intended five source images exist and remain unchanged, but its actual malformed requests do not resolve. Basic font/level/bank/model existence checks for other apps are availability evidence only, not claims of successful unrun launches.

## Human review checklist

- [x] Preserve initial review, seal, registry and raw failure captures.
- [x] Prove same-distribution TBB import/runtime identity and all eight static import closures.
- [x] Stage only two previously absent runtime outputs; preserve all tracked bytes and the index.
- [x] Validate disposable missing/corrupt-runtime and protected-destination guards.
- [x] Complete both full suites, ten focused cases and normal benchmark captures with accurate outcomes.
- [ ] Full suites, Debug benchmark and Breakout resource/shutdown acceptance pass.
- [ ] Complete profiling and remaining app startup/resource matrix.
- [x] Seal a BLOCKED revision; no checkpoint/publication or next phase.

## Final Validation Snapshot

Phase 00, revision 01; timestamp 2026-09-13T03:44:34.596563+00:00. Validated source/build HEAD: `b3b043b15ee814133e8419466a1bf3a985a3c958`; branch `build/cmake-conan-migration`.
Source/predecessor tag `render-refactor-phase-00-approved` peels to that same commit. Initial published tag object `f1a174b35ea0a07b64dafc6eb697ce83f94420c4` and remote verification are retained in the historical seal. Configured origin remains `https://github.com/AlbertBoll/OpenGL-3D-Rigid-Body-Simulation-Engine.git`. There is no approved Build checkpoint or passing reference.

Revision entry receipt `.codex/build/phase-00-revision-01-entry.json` SHA-256: `7a0e6dafd18f1eba9e438ef9d02a629479b11037ada386ce501a3f854ae8fb54`. It retains the original phase-entry protection and freezes new filenames before writes. [revision-entry.json](../evidence/phase-00/revision-01/revision-entry.json) is the versionable mirror. No broad ownership transfer occurred.

Complete recorded behavior/build input closure: [closure.json](../evidence/phase-00/revision-01/closure.json) (`73dcd959516e809cf4751d68721ae15479093891880b4987e360b1a088c40b5b`, 3926 exact paths). Initial compiler/toolchain/source/SDK inputs are unchanged; the explicit transition is two ABSENT TBB paths becoming the verified runtime. The closure adds the distribution, revision helpers, generated runtime configuration and absent malformed resource requests. Input presence/deletion is part of validation identity. Runtime closure limits are explicit; this is not a complete passing reference.

Exact four-class file inventory, entry/revision existence, raw and expected Git blob hashes, protected hashes and checkpoint membership: [candidate-files.json](../evidence/phase-00/revision-01/candidate-files.json). The final local seal adds its finalized review/inventory/Git-document hashes to avoid self-hash recursion, plus exact ignored/generated output classification. All candidate files are PHASE_OWNED_NEW_TRACKED because no tracked production file changed; PHASE_OWNED_TRACKED is empty.

| Exact current candidate path | Raw SHA-256 | Expected Git blob |
| --- | --- | --- |
| `docs/build/BUILD_CODE_MAP.md` | 9fe45f3c9e889ea2d1c9cb5f3a0026751a65e5a243832fb67a59a0579f4fdd2b | bbb88996f62a2c013a564690f969f44e0a762598 |
| `docs/build/BUILD_PHASE_REVIEW_TEMPLATE.md` | 044873dd931077fc3b03a1bbabdbd89a7bf2fa95faa174c1dc42c128a9929b7c | 5f743488f54e6ebffee075087b967de7c45290c0 |
| `docs/build/BUILD_SYSTEM_AUDIT.md` | 269ea393ede846155982e8fc49cb02e174888169eb8c68b48cd7880f0530abb8 | ba626cd34d9ca7977800c80cbd306d6f82d2f81b |
| `docs/build/BUILD_SYSTEM_MIGRATION_PLAN.md` | c26ef331ec2eaf88bb0153a0f11aa289c7d82fab4accf83c4b6099ae1c1abe6f | 595b1a08ddc9fd1e64e9fb8fe23294e2066ac5af |
| `docs/build/evidence/BOOTSTRAP_BASELINE.json` | d55774e0408d2084c590365f834d5e47f7a62f96bcaf6705c080782107bdf3b0 | f6b13a5c5e9b96d0af700de8b43d635fb1121c79 |
| `docs/build/evidence/BOOTSTRAP_VALIDATION.json` | 48fcb76736eb37c82ee99cdc8011ba83c32c23d0d2459c58b3ad066f0c97cbd4 | cd66a9a1b48c9bf9d8a7ab3adebc73eae8fc6b83 |
| `docs/build/evidence/FINAL_MANIFEST_VALIDATION.json` | 24a0ecbf4ec6611c7edbccf9199755f66da043598172c8a97812298df070b029 | a9ac7b80c36d68e5d458b3a129dbc3fa1d616efa |
| `docs/build/evidence/PREMAKE_VS2022_GRAPH.json` | e031105e2996bf15efa7dd8c0d626cbd468b63267925cdb65cad133ba76657dc | d396fd6e16e158faebc2c4b17a0bb166a38907c4 |
| `docs/build/evidence/RUNTIME_INVENTORY.json` | a97988134e863f6a98546ca40f060f086dc94ad0272285afec6108edda17dc3b | d68caa6a026ee53b3fd28fe63dc54fdf4cc91d34 |
| `docs/build/evidence/phase-00/baseline-failures.json` | 4b1d7c496fe6ffcc8ecc349e03667e9475c2f5cc90d72dd3c1203b137524b943 | 461752398d696b6464fc600a1eee57f9efe01f75 |
| `docs/build/evidence/phase-00/build-Debug.txt` | 9d92cec254ee2a83cbeb741f119992142ac97b65c243d882c9105783318cde24 | b2b21448f4244a2b25ef70ac005662c4a343c122 |
| `docs/build/evidence/phase-00/build-Release.txt` | 3435f9e020d421753e555952c7548200f82f8352bb7fffdaa6f376788a1a03a0 | ed3ec3ba507e65cbefaa5c25148804b6754a885f |
| `docs/build/evidence/phase-00/commands.json` | 4dad39977f6fbcc70053cb3014f9f39468ac11522e1b3b142489883acf3e1ff3 | 664e05a309d2f57c78f98f654bf7f817417ec16b |
| `docs/build/evidence/phase-00/compiler-audit.json` | a701836eb12e040fe23f4ba9bc9dc90fa37873e4eb786425a979436300cb0aaa | b1d782d8c54e0306ada75d082e5c9ba73f9ad62e |
| `docs/build/evidence/phase-00/entry-inventory.json` | fd2b70bc2850eecc4fac2a50d01de02f4659be4d54decb63cf09b2947f99960e | 94c27c386892ffa5b94be2dd190a6827ad81262b |
| `docs/build/evidence/phase-00/environment.json` | c81386d326eabadb638d16c18b5dadf96dfd371846a47b9c163ae0077ad0fa2d | 31ff3fc40b8a501cbcf344395f8f93b14f9a032b |
| `docs/build/evidence/phase-00/final-inventory.json` | ec67f251c7bda4fb72cd90b6d2b87957619376b4b0dc1aa43586a72f1b1eda82 | 9619f66e0933495768bdee7304baef3d0d98ce90 |
| `docs/build/evidence/phase-00/git-final.json` | 2e1fbc41ce7aea315a8aebdc831c9f2f17d82f8870d63ba2ffb2d4d3be970cdb | 4e17243becbbaf9c3ac63fa3bf433655d8458d8d |
| `docs/build/evidence/phase-00/imports-Debug-Breakout.txt` | 5cbb693e69b6b929f03636125d29d2b0243760ae8c4fc23b64f94c0bf9290939 | 5fbe0525af3188faf8b471516dd1117b2a755b03 |
| `docs/build/evidence/phase-00/imports-Debug-GEngineEditor.txt` | d4ef594d201dea9df13f54ad549eb121e6e4fe8d27826d8f3987304216b8aed0 | b84a029d75e5aeab59e2ff932ac0a240579da4b5 |
| `docs/build/evidence/phase-00/imports-Debug-RayTracing.txt` | 02b221766a7f5c232b7f4d7df8bacf7a730e0d5c7d2f59f143e0927ffebc8d76 | 1a61288fa6887d2fa2021ea719947066fb63aedd |
| `docs/build/evidence/phase-00/imports-Debug-RigidBodySimulation.txt` | ff8a06a513723129012bd6829885db6f2502e3f35227a7a038e44ab6507c7a06 | 8bccbca2b9ed8176a89251fea101560f58da7cec |
| `docs/build/evidence/phase-00/input-closure.json` | f8ad5754f78fb51e3a19f5b361a3e9b5c3d5e58cda87b3632b41d1e8980512cc | 40eb53337cecb5571b9f77f2ce0cb93f81db5cb3 |
| `docs/build/evidence/phase-00/premake-normal.txt` | 45586c0406d561dcf80b5c8ecaad2d32e4c7c5306ecf9e5ad76f39ed695ddd41 | 14b212b9cf069ee273338f6dd7eb56a1ed0c034b |
| `docs/build/evidence/phase-00/remote-refs.txt` | 57ddf9f1fb7b49ea8dae5b9faf1ba4e1e8d2fca4243c05771bfffa8b4c021c76 | 026e88357c3aaabb192e7b1d3e5fdf4b6c3a4bc6 |
| `docs/build/evidence/phase-00/revision-01/baseline-registry.json` | b962d6ae989ea7dcd0b30432cde1e8299c115c473b051be1901989cc1482f493 | f5d13e456ebe4db675de88c84f8893ba75453c07 |
| `docs/build/evidence/phase-00/revision-01/candidate-files.json` | Final local seal | Final local seal |
| `docs/build/evidence/phase-00/revision-01/closure.json` | 73dcd959516e809cf4751d68721ae15479093891880b4987e360b1a088c40b5b | c37758ff1697dde372863ec24b8ddb2717c9585e |
| `docs/build/evidence/phase-00/revision-01/commands/benchmark-Debug-normal.json` | 477efd8e148cac700e64f526663b4499b9d35ea6e3439e1ae9050902aa7c531d | 5ca6005c97f7c452a8cd9da0514f4d61c7ad679d |
| `docs/build/evidence/phase-00/revision-01/commands/benchmark-Debug-normal.txt.gz` | 9ceffb7310338057cfe71a4ae1e2c98d2c485d81cdef906532a801f457a38d64 | 86e43ea3218b5890302b754bbf8927f94e79f7ee |
| `docs/build/evidence/phase-00/revision-01/commands/benchmark-Release-normal.json` | 270bcca0e77a7b954521892fa78f3fe005951ab5198c6fb45b21f273bc5ab61f | f32bc6f659ccec61f5829c3ada8c989f76cff250 |
| `docs/build/evidence/phase-00/revision-01/commands/benchmark-Release-normal.txt.gz` | 3f533c03af22c8ec376831c845bf09785fd639026dead3d2b61e16b2adb9c52f | 19f808ced191ef6348faf09db102dace75af0020 |
| `docs/build/evidence/phase-00/revision-01/commands/imports-Debug-Breakout.json` | 557ed8aea9d648e1ef1e7a5a0df0b22767d2a36134f245971a45d0fa3de4a077 | 24c6ac12486be68f5829892f810f1f25931998e8 |
| `docs/build/evidence/phase-00/revision-01/commands/imports-Debug-Breakout.txt.gz` | ff43bc5d797ba7a5643c583e4b3ccba9a58ec6286cf0835570517c27b9b564a9 | 7fc86475ec1b8bc066623024eed91920a5a85230 |
| `docs/build/evidence/phase-00/revision-01/commands/imports-Debug-GEngineEditor.json` | fbac10d6aa073961162052900e9f76a14d630b6aac654ecf0554a1f14f4c5f98 | abacc646e31b933eee6d5bb897f23716be0faefa |
| `docs/build/evidence/phase-00/revision-01/commands/imports-Debug-GEngineEditor.txt.gz` | 13d77efaf7f0941d0b899b4fda88c31d9c22712e73f77435dbfc8e5e184aa595 | 704c38c41513fd9f36d79f56f54da3d4a785b3c7 |
| `docs/build/evidence/phase-00/revision-01/commands/imports-Debug-RayTracing.json` | ab95889629a10af0a1cc586c98e7379dd8b353cf54b4eaa57f0625863336341c | 48f42f4e3af5058397930ef879e4c469706a389b |
| `docs/build/evidence/phase-00/revision-01/commands/imports-Debug-RayTracing.txt.gz` | c3f82d7a41603bb83a0f6d52adb16bf3fd9bda18897fdd5813cb722c0e19d054 | e84a4b33510fcd11ff227d9ab3d5c6615c4af57c |
| `docs/build/evidence/phase-00/revision-01/commands/imports-Debug-RigidBodySimulation.json` | de55808caaa12aaee043973740e743456c538198f7be4e23fe924d305b392ad3 | 1274441ff28da05be1ebc72bcc9aa548cd6885bf |
| `docs/build/evidence/phase-00/revision-01/commands/imports-Debug-RigidBodySimulation.txt.gz` | a20ce362e0b8c9d140f9f00356b9aa56c95110e9280f436909dca0fa0fdfd6a6 | ddca1601e7c7a7b41b8a4763bebb87922f51a5c1 |
| `docs/build/evidence/phase-00/revision-01/commands/imports-Release-Breakout.json` | 08808bb9734dadfd322b7b8f01ff8ac5254405bc274dc09c4edd4ba28c8a670f | 38a28d26c81bdad320a65e5b4c45c2b242352c59 |
| `docs/build/evidence/phase-00/revision-01/commands/imports-Release-Breakout.txt.gz` | a5737a367204ad2924527e8b82c353f1f1608d7ac7a23fd93ce1a1ab9fc5b44c | 53a4d8e6371c5de8c32010b77b6ba3308929a635 |
| `docs/build/evidence/phase-00/revision-01/commands/imports-Release-GEngineEditor.json` | 4bea4286074319fd69a5c8715ad522fcfb936eb8e6a7e9a21367c55515220f79 | b34916875bd59fa2e2931638dbbfdca73ecd23ca |
| `docs/build/evidence/phase-00/revision-01/commands/imports-Release-GEngineEditor.txt.gz` | cd9875beb059bf3ba9ffd04dcd136e6c3dfc29ff2aa981bee9ef6f4769512466 | d7e6da349f2eb42d5dc996693e5ab320285e5643 |
| `docs/build/evidence/phase-00/revision-01/commands/imports-Release-RayTracing.json` | 92d6aa873fce10ce7f598ef22adb0f603cadae7515d9c68b3ecd58730d9f0013 | d12dad9e27f1a2a88eaa6c02db34fff210519802 |
| `docs/build/evidence/phase-00/revision-01/commands/imports-Release-RayTracing.txt.gz` | 4ff1d47d4e827c3f2e9141688098e670fe92b53d67e5d9fa170a80e8f7a9e1e6 | c187d5b5da61706c8597abfa08a15bc9d5dd4007 |
| `docs/build/evidence/phase-00/revision-01/commands/imports-Release-RigidBodySimulation.json` | 21c786673d761c312e03cf248dedf5c7fac665ead199540478172ed25daa6021 | 51b765824432e4c7503a2b7c515828d6fd9deec1 |
| `docs/build/evidence/phase-00/revision-01/commands/imports-Release-RigidBodySimulation.txt.gz` | 47f40cafa2c5c83058ee0776d067e3d1bc011836ad86d2be0e74b3e43365ead4 | d8b90df7bdc1dfa5ef21019e638fd867ad3de0b6 |
| `docs/build/evidence/phase-00/revision-01/commands/runtime-safeguard.json` | 64d8dc443f559889c2fec22de4aa107f2e280036f3693303aeed334c97ffa5d6 | f892fc2b6bcf0d1e8732b20b84ec5e47e513b744 |
| `docs/build/evidence/phase-00/revision-01/commands/runtime-safeguard.txt.gz` | 4e4ca58a57111e6e9c318a00b0ac22de2ff66ae6d43606cd7102e6f8b534ead0 | 40239559995f680ce1e1f819d5adf502ea5c6d36 |
| `docs/build/evidence/phase-00/revision-01/commands/runtime-stage.json` | 7360b4c540166e24ce06e26e7de54dad444acef8bf9496245ace7375621f7d7b | 130e256f62e901387920f222bd57a656e4b40791 |
| `docs/build/evidence/phase-00/revision-01/commands/runtime-stage.txt.gz` | c221b87d7a889f79a0eda08242acdb1df36d747ce530a2543fedc038bf025815 | ffed36eab5f8ce6d46edc09a980c1b8843710bd7 |
| `docs/build/evidence/phase-00/revision-01/commands/startup-Debug-Breakout.json` | 837a8a5574560e0ca9cc7f8736785d980a543617cd3902ed57914706461162b9 | 191a8d10cab445c01c99899e9f7e7c18f7847bae |
| `docs/build/evidence/phase-00/revision-01/commands/startup-Debug-Breakout.txt.gz` | 5b91b556fc14c38a0d241503abf8797ca0afd9c28c50c0d2f28c3e107e3a54ff | 57564b59e23838f9ecb632f99918e9d921fba4cf |
| `docs/build/evidence/phase-00/revision-01/commands/startup-Debug-RayTracing-recheck.json` | 1ca0f7af41f1e2ff4078a2c26b75c899f516a14e84855666d1c12cf474ba47b6 | e851d05c8d015c37db6e44ac89d0fd0ddba326a8 |
| `docs/build/evidence/phase-00/revision-01/commands/startup-Debug-RayTracing-recheck.txt.gz` | 4221e647e8f0a7a2da132b9e2dd415d5512e18d3663be850123949d538c7f9e9 | a91fbd7eaabc944c808ffcba7947cfd3f3ae2c71 |
| `docs/build/evidence/phase-00/revision-01/commands/startup-Debug-RayTracing.json` | 08e6e0d9f76fd50a30a3f240ab71a735cee0ff96da6fed8e6bdc2c589485dfe1 | 4fb2089507248d2c2d71ac0c488f82c3a5502a71 |
| `docs/build/evidence/phase-00/revision-01/commands/startup-Debug-RayTracing.txt.gz` | ec140410fe499c48de61e46c8c3aeef8f8842ad68caffee7d88173c507455e45 | 193ccacec96a6d5448676d55010c60568972e8f4 |
| `docs/build/evidence/phase-00/revision-01/commands/tbb-provenance.json` | 09ea36f382b6ee24aba8a306d16a4f6ee5b9856ff61ba9dc2f170be6ffba600f | da09881adcfb4bc27b8e2f3904e345cba04919cb |
| `docs/build/evidence/phase-00/revision-01/commands/tbb-provenance.txt.gz` | ead8ecfe9220424644e8b18d86d344a5936aba5e34af63831edf8bd99858e9f4 | be9da5538db138d8191660c7171e4c430e19c947 |
| `docs/build/evidence/phase-00/revision-01/commands/tbb-runtime-identity.json` | 35b59e9d5e846849b5f17f1593aadc29e2207b1c9cb46f2a5ba624930d02fdce | f25128361b732c8ee40eac70eef2cd83a61b984f |
| `docs/build/evidence/phase-00/revision-01/commands/tbb-runtime-identity.txt.gz` | 1dd69386923ae94c3b0c100d0dc2c4a02abb2a92934c497225399237e53327fd | b5abe852e521bbad546ed7df99b0013d42b80032 |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Debug-absolute-scaling.json` | 500da95c820ae9c0a79cfcbb41dbea4d1eaeadbf663b54f863ab3e5f1cc406a1 | 01c3a3fd95a3963427c235a065ee8d8a6e0d39a6 |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Debug-absolute-scaling.txt.gz` | 096590597f3aabee3d5a3fe79778f5c1583f04d32ecfca9baf3e8de7bdd48184 | 84fab95cca50e99ec8ed5a30e59acd989fb82a55 |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Debug-fixed-scheduling.json` | 2e418e0ae3ce6248742396584d5c636a45a0dd3dfb916f5eed2e25ca4807d3ce | 7257a91a8c66c2a43a6426aa9b339248f867bcaa |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Debug-fixed-scheduling.txt.gz` | cdfe3723fad5a79a2edd523bd1a281c2d130b91c8c8022d21bf09b8a5921213d | 971ad5fd32d526801c548f1caf262953af28d5cc |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Debug-full.json` | f748675edc138a30ae84b92bac5ed01f18039cffa094e308c83e0df93d7a056e | 38de1f6ef6ef6a74de6e50396e32f55b636f4d56 |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Debug-full.txt.gz` | b19aef94a491b59312fc07c9c19b6fb964ed143521dcf91498313926092f10e9 | a0259d4f657ab5a5a1044a4faaa87bf9d517e696 |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Debug-runtime-transform.json` | 558289acc711d71d6a64bba0dd8338f631140b889e1b228a71640e77d53dbc87 | caaf58c4d69793bf0f043a939bf2e7f6ee6bf2ab |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Debug-runtime-transform.txt.gz` | ecb99f3d83a06c806d37c301b5a0754116c3ee5323bfa26069fa5256e97c6bc7 | 20020e4b7daafd3279e24708d35752f342008529 |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Debug-scene-runtime-lifecycle.json` | dd2f8e87c5602a11fcaeea5435e9f7fa0b47ff6b59ba2a510f3c0a167b22d934 | 70a8f12d98751a13c96e4facc3e86423fe64e785 |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Debug-scene-runtime-lifecycle.txt.gz` | 2ef546743697459b356286f25079a97c1698a0e56853ba91b0301143be242456 | 17eda3d8e33a303f44e33142df993b5b7dc8c498 |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Debug-solver-storage.json` | 325a21d5dcb0803e104f4d77cd15d7154aaa64c297a26b0301509b1eb3d5a76f | 503101eda8b63011b6179dc7fc70cd7c4ccc8c16 |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Debug-solver-storage.txt.gz` | 9e6e26e237141150b44c71ada539bf1b8f7b1683340acb03363e23e3c6ce13e0 | 64b3da7df6cb7d4f3c12d56ac61015921ea188c4 |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Release-absolute-scaling.json` | 74efb47a6140e8527b10e188630f2bce982624a538e5a84ff92d7cbae3453832 | 716ea6e453d822fc8a55c9a8ffef8a9eeb5ec65b |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Release-absolute-scaling.txt.gz` | 95f9bceeb56d48fd9965ade60735dcebd7d13d6eb2b44b3d382095446d7a243d | e89dc8a734e25c56bab3ff873f6006f6e0f5815c |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Release-fixed-scheduling.json` | a100956099eae6ced51e1c1c87fe3fdab3dd270f881e0f5accdd5855d09e8cb4 | 4b256c7ad5e4b0d40d0bfd80ed83c0558cc9c8f4 |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Release-fixed-scheduling.txt.gz` | 4ce3f651357ed456f172640faacae2df6de09dca6e602e91f1f6a93841b42fc8 | f6a0159023a30fde0e84372273ef199c8b8242a9 |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Release-full.json` | 4b0db22b80f5c030701e5fcfc9e61982793359aa47b782e113d31fb093b24fde | 6bba4e9308bd6d84b0f16d6e86b2a6adf1cf8ae4 |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Release-full.txt.gz` | bbfe5a8a21368a2ce91e2b99f7ed739cb89f38ec7846e5621cc245f448d66d8b | 218d2412568bdfc4ad4de22f6c4937d6bbcbb1b1 |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Release-runtime-transform.json` | 54ef5ac0f90d0e89e558c413b33b71e423a8744b7039b1474760e702c78c7df9 | 4e8e3073c22707bffe23ad6eab57343aa1d54ed2 |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Release-runtime-transform.txt.gz` | e06982db2861bd9d4f52e208c68bf2ff7e140a878ce3f895379479a0f9a59a74 | ec61a4561131e41eca26363426d95cac8181e698 |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Release-scene-runtime-lifecycle.json` | c2b0222c55b088210e448506a08c440b6e79e5a5e1e89b378a54ee9ed39dfb3c | 5c18cb557705e83c0743e8ea5b6d0cacdd3cfa5a |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Release-scene-runtime-lifecycle.txt.gz` | 2ef546743697459b356286f25079a97c1698a0e56853ba91b0301143be242456 | 17eda3d8e33a303f44e33142df993b5b7dc8c498 |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Release-solver-storage.json` | 82aeba60a658beda239265c060c7e71d1efeed2f9218d418b8343c10e8ef76f5 | 04c381e678035c7ed82d84492f8241e3060d6e44 |
| `docs/build/evidence/phase-00/revision-01/commands/tests-Release-solver-storage.txt.gz` | 9d619dc09bc8c54cb944089a74a194f7affb5ffcbae6af64aedc7eb69abbd544 | 6cf08cee5a0b0bae46e48c7d5b6aab6e69f98519 |
| `docs/build/evidence/phase-00/revision-01/environment.json` | fc78e2d69029a4428eab90a521662826e0d35ac0a0fb72ded96ef73b5e951c99 | 823f6099ec6496ed3939470f4eef3203b278c676 |
| `docs/build/evidence/phase-00/revision-01/git-final.json` | Final local seal | Final local seal |
| `docs/build/evidence/phase-00/revision-01/initial-blocked-review.md` | d145c2b2b6fa2d7c9643f2cd40f1b6eb1fc58635f77be92d831230145a9e51d2 | 39c5d9745d8addb6397b4fa46a539c5a1a345834 |
| `docs/build/evidence/phase-00/revision-01/initial-blocked-snapshot.json` | 86621b4bcbcbd46a79eaacc35de270b0b833b6eda9b1138a503b2b1eeb1f2a70 | c95163ee0dcd2acdc65cd0c572dfd083a5bd1fa2 |
| `docs/build/evidence/phase-00/revision-01/resource-audit.json` | 4644c900883c2e11e0d68280d9d059cadb38b56cb88e842abcd43313cbd61e79 | 5e5adf07600c5d89aa9218041715c7a6b6980dc9 |
| `docs/build/evidence/phase-00/revision-01/revision-entry.json` | 7a0e6dafd18f1eba9e438ef9d02a629479b11037ada386ce501a3f854ae8fb54 | c675ad5acd8fddeb7a4618891cac7b4576f1839b |
| `docs/build/evidence/phase-00/revision-01/runtime-audit-after.json` | 3e34439a82da90fd3dfb9e75ca38e099f0ebe0e062dccc50c010f2fdb5784735 | 8ba3c2ce2550b99f259668270c8a6760ce39e2e6 |
| `docs/build/evidence/phase-00/revision-01/runtime-audit-before.json` | 1afc7578330d8c089e8941b607c3abeed6c29428c554b14cbc0abf4558ea7965 | 1ee3fea64aa8bcb16e4fdbf09e4eda9a01a4684e |
| `docs/build/evidence/phase-00/revision-01/runtime-plan.json` | 7cd1ea654ce94c7c15b3367de3c57f3d2e158ebd2ad4948e08883a54129a721f | 26ee1a3ac39aed546e18b20c83c31fa6aa2a0f45 |
| `docs/build/evidence/phase-00/revision-01/safeguard-results.json` | 035a4e58266876b48588eed76574ffc67e64a319cadd6e5515e65c89f149cae2 | 58e1fcac3bb608ba54e6182d4e3ac773f5da86a5 |
| `docs/build/evidence/phase-00/revision-01/tbb-import-libraries.json` | ebb98c2b91e8cbee614eacb4e96669820b105ebbcc4a95e1a7d066e0610bdeb5 | d024cabd486f7621fae70bbb5360ee0929bb9a3d |
| `docs/build/evidence/phase-00/revision-01/tbb-package.json` | 4bbd4baa7afd20a7e70f343f58878e9f9fd42aa020b36a5e784699925af33098 | 5be9eb9be6af1c6708fc8441444efbc1a674cba9 |
| `docs/build/evidence/phase-00/revision-01/tbb-release.json` | b7f9b01ecc120bddfb2e7bce9add07875b7a575bb6736f12f45d7815b67c1413 | 42396ed62152e2da29b517d8015df99fd65ab419 |
| `docs/build/evidence/phase-00/revision-01/validation-summary.json` | fe12d5a130879a6ca57d8b8b51f16a9a23ed72acf0fbae25e0382570dc8cdf83 | f7be327f8c275092723a7642f8acb411982a0a46 |
| `docs/build/evidence/phase-00/startup-Debug-RayTracing.txt` | 9ddaeca3e9efb76604ce8da2ffa014e06ed8843fa4d7a39ed1e60a4752beceb2 | 24d24fa0315f36afde33325ee3505e3829d12b62 |
| `docs/build/evidence/phase-00/tests-Debug-full.txt` | 3de4a8fd48d65b4c999041606b2e2e18d1e6ab7870a7b055b908a276ef03a4bf | 98cdcf87447f44beebd80b495009631cb445ab71 |
| `docs/build/evidence/phase-00/validation-summary.json` | 9902a0ff93da8265fbd79685da7eda193a1421f708eb1d60768127331d11ce47 | 6edaac5e3a35a94ea41c58b7564bdaf0d588ab0a |
| `docs/build/phases/BUILD_PHASE_00.md` | d61e196582aff4d667b284b5eacce0f614ab7f13b7afff4f8c1dd5dc1a79cce5 | fcff609f7a91912e3ff969e44c4323ad693dbcac |
| `docs/build/phases/BUILD_PHASE_01.md` | d907d4d90c36e940c4f5590a64f8839d31a15fa522a842d1d78ef1bf634e9ad4 | 0b5b8562cf75530afe02c42be8acf1e56c86dd43 |
| `docs/build/phases/BUILD_PHASE_02.md` | 84b13e0ec716a726b9207436ca10d268e41ca83ce26e1aaf53faa6de60b245f5 | 4be62b4ada5ec86b9f16455735b59d2ce06e059f |
| `docs/build/phases/BUILD_PHASE_03.md` | 0a2c8d79722c413cbb40b8c62ce7176710dd8b8d90fdb4ad17bdfafc97bf7c41 | 7d506c8cbcb49ed4c0586666d9a9a2d3c8a9ebbc |
| `docs/build/phases/BUILD_PHASE_04.md` | e77022c23677eeb9146904bb570d99813a65ef14898c2fe92c0b7364cf8cf030 | ae882dd1b210909ce602807e8b1cd0b1a92dcd7c |
| `docs/build/phases/BUILD_PHASE_05.md` | eea04a5f048782019aa00f6c49133c895861807f6c1dbb7c2e620f1cf6d5c43c | b02d528bbf3d617c33cd19ec75c6c23ee993741a |
| `docs/build/phases/BUILD_PHASE_06.md` | 0d65d34590eb8c6f965ff1adf8dd3f52c766c091d82eb5c9373083f6aa16296a | d05247647aa671811b0b3c0e0bbb9b69ed0c77c7 |
| `docs/build/phases/BUILD_PHASE_07.md` | 4b7cb0788a122e36fdcb0040d288bc4d2b5ea160cb637f2b2eb94e9ee4e81449 | 96da4503b131a0ffa8cb1fa28acde187e27c8c01 |
| `docs/build/phases/BUILD_PHASE_08.md` | 76a4941e77e72a69b449610c8ac3a5d365d0ff67f8d8ce7684a65420222d7718 | 0cc3c03efddd10214a5adc72ff6b487770b0eb44 |
| `docs/build/phases/BUILD_PHASE_09.md` | eac62f1736db784fb4f5cd971a181f443bec4105432b487fcaf589d5f2ce0ee6 | ec338c39ce48cec6342f330883bc7d12ac1009db |
| `docs/build/phases/BUILD_PHASE_10.md` | 27b2aae4de3a25ba997a361913d6c60a920dd6ec1e51ac786cc7ce79e9534f95 | 48b6e818f8fbde50bc9604d07fe3668124897083 |
| `docs/build/phases/BUILD_PHASE_11.md` | ddfd8ef34da1aeae590f35137aff488d0e58b3d8c17d62e1ccb916ad5da6978a | 71ee8520b9e6eb8de47d19cb47b8c2e69a26fb01 |
| `docs/build/phases/BUILD_PHASE_12.md` | 5789137c6fa31f65b499ebb03dc0a362421cde227ed48fd6f396281711360800 | b97d17feadd4d27a8d5685b06333c63c7de0bcfb |
| `docs/build/phases/BUILD_PHASE_13.md` | 320fb2ee5636975ec50be708949a088d30826d9db35ffe198010215c9bfaa21c | c0a7057e5ebe49877d10979b2b8bc2aee0b3a372 |
| `docs/build/phases/BUILD_PHASE_14.md` | 10e621b207017409f136b10cd3800187af5ffc5140311c1ebfd389c5591483a1 | f0d2b909033b6f8e8b6fd625b8a3a80ff2053325 |
| `docs/build/phases/BUILD_PHASE_15.md` | f54c8eaddf5b01da8f2dcd2e13acbad6b018665dda2eaad3e3e9ad9d08df9877 | 117c8b8680086684e4a8f44b4ffeb41e927f0511 |
| `docs/build/phases/BUILD_PHASE_16.md` | b824be7b21ad0c53931eca81eb682b8af49ef27edfc984e9d5b0e64013c3d4c1 | 9bbdd53a16b7d13c05a551559f10b65b45918941 |
| `docs/build/phases/BUILD_PHASE_17.md` | a1f5a463a0de1a595f2fe47c73adeede23d530e21cfbd6721658dab8126545d6 | 1259bad30df34d20eb2948c4972acfb03ae95ea3 |
| `docs/build/phases/BUILD_PHASE_18.md` | 3d7e406d332e4f39c5eda2bab955b0604853529eacc3e35645c5e0ac1c0104a5 | f5cf2f952db5966f729bf3ed7bc26a44e1c21e28 |
| `docs/build/phases/BUILD_PHASE_19.md` | 9cd81f99cb883bc617661a5d99b67543fc6da0cf3e97438ef0a1a819c88911d9 | c14a9d61de036bf2a06a30007b84f869e947c27f |
| `docs/build/phases/BUILD_PHASE_20.md` | 211160dfc5ca68506a89cda8496cce705f5d86a5cbd686b9841c6552416e35ca | 8138dee4025e53e880849162ec0979524886dba0 |
| `docs/build/phases/BUILD_PHASE_21.md` | 759f02f5aec438fd86f60a49af90042720193aad3037ca64267c33272e8d5515 | 5bedd8c5e4537328f25bcd315833eb49f1888753 |
| `docs/build/phases/BUILD_PHASE_22.md` | 7432b14a07b38af1cd6fb9894dc0c4ab85160f871be4a75818cf3b232c54cd98 | 22c6a5cbd426ff9196feccf5acef3821da76d40f |
| `docs/build/phases/BUILD_PHASE_23.md` | f1947e1293e4d58412850f68a916cf6a47e7506e5fc22ea0e4ff0bfa36cc819b | 90df4799eb0b203dedc1c68a173b9bad8c1ec8e4 |
| `docs/build/phases/BUILD_PHASE_24.md` | 32e0c9eb577e62c122ad7f2be0985f6b23605d525f89e9d8083d0351730d56e0 | 2765bd24be24fabaf34eb4bba3587491f4caef8c |
| `docs/build/phases/BUILD_PHASE_25.md` | 2fd4071a8bb5c32435455b5865c3040e436b3d3073314a056e8a66be1fe1aec3 | e4366ac5054cf812122fe9f0f3744862da361d2a |
| `docs/build/phases/BUILD_PHASE_26.md` | c5fb04ab533d4841c382ab3065d00f375bdc0d82e3490230ce4a0a5553976b29 | cfd6cf52ed59f68dbe6ea1a0e1816cd165b63a58 |
| `docs/build/phases/BUILD_PHASE_27.md` | b8d2de0dec6ca9f1ffa638e5407e5230a43ebf3896bf83037046c1cfcd056f1e | df066e32c88f167f967df42c6f4f4f627c6d3926 |
| `docs/build/phases/BUILD_PHASE_28.md` | 9e124c5d34864f3f2637e1f1763f5e93ed5515e19d45b0b6349334b2acfbc94e | 59ba4139f6852c0ec0b256dba78520bd49ced2d7 |
| `docs/build/phases/BUILD_PHASE_29.md` | 4a0fab71aa51374d9d94a52a6613d64d251c03241b959ea96f87ab9ed1252dc4 | 730626eafc6596e8cd9020179422355728737736 |
| `docs/build/phases/BUILD_PHASE_30.md` | 392d74217c8f7b6691cdf1c6a501cf2225b8efa26a052ef6804b6d98e2c711d7 | fef9ade5fe31c4dbd6c1f187e976541ba3bc6fb1 |
| `docs/build/phases/BUILD_PHASE_31.md` | a613a5facfefa8566ee6a1506a258c4438fcd685f2d775a4d474939e48194808 | 1c0f4d0e0e4dc8502ec769adc3e47660681c65c8 |
| `docs/build/phases/BUILD_PHASE_32.md` | 2cc4757e72ee1ae19640f3810ea528eb4548fda3cce1c608bc7a02af48ded166 | 1aa141da1774ef474f827cc169d90d5610093639 |
| `docs/build/phases/BUILD_PHASE_33.md` | 0be8617fbd6e8f5d65aca048c3d8f7cd6808dc8fdbc649625759c718b9dc6dc3 | 2250764cf1fcc238b836998efc2f059f32fb5b44 |
| `docs/build/phases/BUILD_PHASE_34.md` | 6ce9a82e357386f1cf5326ece365d05225976cba80379d7ed2ff38a2e10c377d | 8aa6575105ab0e8b060254fa8954b8c6c1250d5e |
| `docs/build/phases/BUILD_PHASE_35.md` | 788c39988af75699728a0b4769847c8ce2c9c7084d71864a8399ae8bc1890f85 | f39b39ca500bd19e6fa327dde7f2f4d8afcb0147 |
| `docs/build/phases/BUILD_PHASE_36.md` | f6c6ed9b273a9c21049d205209ae28181de00d37c0c0559fbed4a78e80d24d5b | de77700c8fe54fe01649388e1d2638922e69e5ef |
| `docs/build/phases/BUILD_PHASE_37.md` | d255468064e4fa1a57f1fc350c1443bafc990ff950fc449dc1da316e66c61700 | ab338491ad3dd385de72995234b04f1cfb577d3a |
| `docs/build/phases/BUILD_PHASE_38.md` | e6095cd9f003f57a32583258f33697b68510cbff0b80b288fb7124f791e3404c | c27be03092e1cabd11904626a90a87197eefc279 |
| `docs/build/phases/BUILD_PHASE_39.md` | eaf13a759fd16917137bfe2941357bc7f5d653ed51d6ba314781fbee91895cf5 | c41b98a09062c20e09b38bedf824b3965edecf4b |
| `docs/build/phases/BUILD_PHASE_40.md` | 910ce35e9c410f212f40ff5630415d40017652e6c9d2c3789824660afee26845 | ac89216a1b1ffd507003207027f745bdf8c436a9 |
| `docs/build/phases/BUILD_PHASE_41.md` | 82cfa36b09708a1ad96bd6d61b3496cceb809e1d3f289ebc41d644e8ae6043d3 | c37aa1c8316391c2646dd3af366b75efa889bddf |
| `docs/build/phases/BUILD_PHASE_42.md` | ab3b83dd13f4aaa36851cd9f01dc061db69d2b8e7bab5e699f2fe4711ac903aa | 9bd628530cd578a46fb483441c704e234ed250af |
| `docs/build/phases/BUILD_PHASE_43.md` | 21b9f753464ee4a39d1b2967580338561147a6e39835a64b3598d1d117d9085d | a8d27696f1f4c82e07d13f2b6dbbfe51f5e3eb8c |
| `docs/build/phases/BUILD_PHASE_44.md` | a44eb75fc6cad75e7927c9c1bafc3fac055de3b98d231e8e83a7c5610d9103ae | 8fc4c7bc836a2e4f667a1400740f4f78243ffd72 |
| `docs/build/reviews/BUILD_BOOTSTRAP_REVIEW.md` | e4973c0d3eaafdc72ef9a731d1606b628008e0e3177c461fd1ce245021dab4e2 | 45ef2aaa9ad33a0994bd6f924d199c93691a1d09 |
| `docs/build/reviews/BUILD_PHASE_00_REVIEW.md` | Final local seal | Final local seal |
| `scripts/build_validation/capture_phase00.py` | a2ee47051c325b2d81b4daa901e1550239f0286adc4dd842deae584c3a8c5638 | f9350377c7a1368481d7061d79f795ff0457f113 |
| `scripts/build_validation/finalize_phase00.py` | 9a10ff87039a370644188d454a4ed26d84de03a65bc4c41e05f68531a830d363 | 832ced578cff9640e53d16ea41a34417e718eea0 |
| `scripts/build_validation/pe_runtime.py` | 51a824e79e5e57f05ce0d18e6d91670cb5430be441687ca353e0044acdc1e2fa | d1cdcae36fc52762b2c1bbbab713f62a0425d14b |
| `scripts/build_validation/revise_phase00.py` | c76b019bf7fc6ca31ed48c0a8ecd3e12f8e85c4a3a2eb036d63eec685d5120f2 | dd15a0611abea8c154c0216227d38cc91f39e52e |
| `scripts/build_validation/seal_phase00_revision.py` | 773fb27a08461c3c027a93af5f37f814d59897ba62c2c2294b7bfdaf198eb12b | 00ee608b26349729d69d5cfa309f076fe36a6502 |
| `scripts/build_validation/stage_premake_runtime.py` | 383968b0ec159d84170d4b0a9145aa571f8321e94dc1b9a1a75dcdbf8e68599d | c540b6217a5ecf0c4e631847dff5348f68b25ff6 |

LOCAL_GOVERNANCE excludes AGENTS, MANIFEST, skills, state and local receipts from checkpoints. Protected unrelated tracked files and original candidate evidence retain their entry hashes. New validation logs, copies, downloaded distribution, .ini files and two TBB outputs are generated artifacts. GEngine.log is the program's generated log (Log.cpp opens it in truncate mode); its updates are output effects, not overwrites of historical captures.

Literal `git status --short`:

```text
?? bin/Debug/RayTracing/tbb12.dll
?? bin/Release/RayTracing/tbb12.dll
?? docs/build/
?? scripts/
```

Literal `git diff --check`: empty stdout/stderr, exit 0. Index entries/hash are unchanged: `6636606189e853606359cd02703c8da0c2b85a45bfc6968e1d405ddec2d82128`. Expanded status and every ignored path are in [git-final.json](../evidence/phase-00/revision-01/git-final.json). New-text whitespace results are recorded after finalization in the local seal. Original verbatim MSBuild logs retain their already documented whitespace diagnostics; they are immutable historical evidence. Gzip new captures are compared as bytes and not falsely reported as whitespace-checked text.

Context expansion: actual Debug/Release TBB link read tlogs and Git history for provenance; official versioned archive for byte matching; PE direct/transitive imports and actual loaded modules for runtime closure; current source-CWD startup/Log/SDL/ImGui code for capture semantics; PhysicsTests:2682 and PhysicsProfile source for the completed telemetry failure; BreakoutApp/GameLevel, AssetsManager and Texture loader for the observed resource-path scope stop. Original broad audit/roadmap/history was not reloaded. No Physics/rendering behavior repairs were attempted.

Local seal: `.codex/build/phase-00-snapshot.json`, workflow BLOCKED and approval_eligible=false. Its SHA-256 is stored in local BUILD_STATE; it does not hash itself. Mutable control exclusions are BUILD_STATE, the active seal and later approval/rejection receipts; all actual behavior/build inputs are hashed. Any subsequent candidate edit invalidates this receipt; any behavior/input edit invalidates affected validation and requires authorized revision.

## Approval and rejection boundary

The exact potential checkpoint manifest is in candidate-files.json, but approval is blocked and no file has been staged. The two runtime DLL outputs are excluded. No commit/tag/push/approval/Phase 01 action occurred.

For a later authorized rejection, use the rejection skill and original source checkpoint. Preserve all 55 bootstrap files that existed at original entry, historical original/revision evidence and unrelated bytes. No tracked source needs restoring. Only verified phase-created files/output paths can be considered for isolated removal under that separate command; no broad delete/restore is authorized here.

Next decision: the user must explicitly authorize any additional preparation contract for the demonstrated source/test behavior issues before they can be repaired. Current runtime-only authorization does not cover changing texture path semantics, telemetry expectations/profiling policy, or application/Physics code to repair access violations. **Final acceptance remains BLOCKED.**
