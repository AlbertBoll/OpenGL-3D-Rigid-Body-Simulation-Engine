# Phase 32 unified loading

**READY FOR OWNER RBS ACCEPTANCE.** The Release x64 Editor uses one global loading bar for startup, the initial scene, Content Browser loads, same-scene reloads and supported scene reconstructions. The first drawable frame shows 0%. The terminal frame shows 100% after the application becomes ready or the scene transaction publishes. The overlay then dismisses. Final visual acceptance is pending. [Candidate handoff](../../logs/pre_editor/phase-32/unified-loading-01/ready.json)

## What the progress bar measures

Initialization starts before the runtime asset check. The first presentation uses the existing SDL window and graphics context. Renderer and shader preparation, geometry registrations, texture acquisition, scene construction, body creation, joint and controller binding, Editor setup and final readiness share the startup interval. A fresh scene request uses warm resource costs. A reload or Reset also reserves time for restoring authored bodies and objects. Measured stage durations set the weights. Successful registrations, acquired textures, constructed bodies, bindings and restored-body readbacks earn progress within each interval. During an indivisible operation such as convex hull processing, the bar can hold while the activity indicator continues. [Work plan and source impact](../../logs/pre_editor/phase-32/unified-loading-01/impact.json)

| Ordinary Release operation | Distinct percentages presented | Outcome |
| --- | ---: | --- |
| Startup with the default Distance Chain | 57 | 0% to 100%, then Ready |
| Startup directly into Book Conformance | 72 | 0% to 100%, including the initial scene |
| Content Browser switch from Box Stack to Book | 25 | 0% to 100%, one publication |
| Reload Book from its card | 53 | 0% to 100%, including restoration of 30 authored bodies |
| Explicit viewport Reset in Book | 54 | 0% to 100%, then Ready at tick zero |
| Close during a Sphere Diamond load | 10 | 0% to 21%, rollback and clean exit |

The checks compared each reported presentation with its predecessor and its earned work. Book published 48 bodies and 15 joints. Input during the modal load produced one scene publication. The direct Book session supplied the reload evidence. A separate focused viewport interaction supplied the Reset evidence. [Presented percentages](../../logs/pre_editor/phase-32/unified-loading-01/operation-traces.json) · [Ordinary Editor observations](../../logs/pre_editor/phase-32/unified-loading-01/observed-editor.json)

## Performance

| Matched Release measurement | Previous | Current |
| --- | ---: | ---: |
| Box Stack startup, median of three | 3.418 s | 3.378 s |
| First drawable window to startup readiness, median of three | 2.871 s | 2.826 s |
| Fresh Book load, median of three | 1.420 s | 1.418 s |
| Book reload with authored restoration, one run each | 2.804 s | 2.785 s |
| Active rendering pass CPU, median of run medians | 0.633 ms | 0.646 ms |
| Editor UI CPU, median of run medians | 0.339 ms | 0.338 ms |
| Timed GPU passes, median of run medians | 0.626 ms | 0.632 ms |
| Whole-process CPU per settled frame, median of run medians | 6.784 ms | 6.796 ms |

Body construction accounted for 1.376–1.407 s on fresh Book loads. On reload, construction and authored-body restoration each took about 1.37 s. The matched sessions delivered equal draw counts and GPU query coverage. All four steady-state metrics satisfied the predeclared repeatability guard. The normal frame entry is byte-identical, and the observer is released before ordinary frames run. The new startup clock also includes the initial runtime asset check. [Cost evidence](../../logs/pre_editor/phase-32/unified-loading-01/cost.json) · [Frozen comparison rule](../../logs/pre_editor/phase-32/scene-loading-01/timing-method.json)

## Candidate and milestone scope

Debug and Release x64 RBS built successfully. Post-build verification found 259 staged assets in each configuration and copied zero. The four affected RBS files are `RbsLoadingScreen.h` for accounting and presentation, `RbsStartup.cpp` for the startup lifetime, `RbsScene.cpp` for resource and binding counts, and `RigidBodySimulation.cpp` for restoration and publication. Adding the startup entry brings the cumulative Phase 32 scope to twelve RBS production files and zero Physics production files. The Release SHA-256 is `117e8fff5faa0eb446ab285b9865c005bd7fadbbdbaeddb9dd435822a7cbb656`. [Release build](../../logs/pre_editor/phase-32/unified-loading-01/build-Release-02/result.json) · [Debug build](../../logs/pre_editor/phase-32/unified-loading-01/build-Debug-01/result.json)

The qualified standalone providers and integrated scene retain their recorded scope. The six-body passive ragdoll is **experimental for visual and development use on Jolt and Native**. The Chapter 21 DROP and TILT failures and outstanding production obligations remain recorded. Final Owner inspection is the remaining milestone acceptance step. [Book Conformance ledger](PHASE_32_INTEGRATED_SANDBOX.md) · [Phase 32 gates](../../logs/pre_editor/phase-32/unified-loading-01/gates.json)

## Owner inspection

Launch the [Release Editor](../../bin/Release/RigidBodySimulation/RigidBodySimulation.exe) and watch startup. In the Content Browser, double-click **Book Conformance**, load the same card again, then use the viewport **Reset** arrow. Check that the stage and body counts change while the global bar moves forward and that each successful load returns to the Editor. To inspect startup directly into Book, launch the same executable with `--scene book-conformance`.
