# Proposed scoped Code Quality policy

**PROPOSED / NOT_STARTED.** This is not the live repository style. Root .clang-format was absent at audit entry. Adoption belongs to Phase 01 after activation.

Use four spaces, Allman braces and a 100-column target; a small justified exception up to 120 is preferable to contorted expressions or broken diagnostic text. One action per statement. Split resource acquisition, validation, mutation/publication and cleanup into readable steps with named intermediate results. Retain useful ownership/thread/error/invariant comments; remove narration or dead experiments only in touched scope.

Preserve local naming and existing public spellings unless the active contract owns a justified rename. Avoid global using-directives in new normal headers. Make includes direct, minimal and private to their implementation where possible; do not reorder dependency-sensitive headers blindly. Keep constexpr/noexcept accurate. Use concepts/requires only for real generic eligibility and static_assert for representation/layout/invariants. No modernization sweep.

Recoverable operations return typed expected results with context and deterministic cleanup; invariant assertions are separate. No explicit throw/try/catch, exception transport or custom recoverable exception API in newly introduced/migrated production. Runtime logging uses GEngine logging; std::print/println remains appropriate for tools/tests/bootstrap only.

Prefer narrow functions around coherent responsibilities. Do not replace obvious straight-line work with a manager chain, speculative abstraction or template merely to reduce line count. Convenience APIs must expose expensive work as explicit creation/reload/publication operations; no per-frame hidden compilation/decode/heavy copy.

Pre-A phases:
- 01 adopts policy and a reviewed formatter version/configuration; no formatting execution yet.
- 02 mechanically formats only the approved initial A files, with lexical/token and diff review. No renames/control-flow/API changes.
- 03 performs separately reviewed semantic readability changes in resource construction and RBS setup; behavior/error paths remain unchanged.

Initial touchpoint candidates are GEngine/src/Renderer/SceneRenderResources.cpp, GEngine/include/GEngine/Renderer/SceneRenderResources.h, GEngine/src/Managers/AssetsManager.cpp and RigidBodySimulation/src/RigidBodySimulation.cpp. Select only necessary files within Phase 02/03 budgets. Formatting other legacy areas is FIX_WHEN_TOUCHED or explicitly deferred, never a whole-repository prerequisite.

For migrated public surfaces, reviewers check API dependency direction, native boundary, error/ownership flow, revision/hot-path work, comments and readable control flow. No whitespace-only tests are required. Focused validation must address a real invariant; compilation plus lexical verification suffices for strictly mechanical formatting. The formatter proposal disables automatic include sorting pending dependency review.

