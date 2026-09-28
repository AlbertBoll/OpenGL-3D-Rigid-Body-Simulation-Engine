# GEngine scoped C++ style

This policy applies to new and migrated GEngine-owned C++ within an explicitly
authorized phase scope. Adoption is reviewed in PRE_EDITOR Phase 01; the approval
status lives in the active workflow records. Policy adoption does not authorize
formatting another phase. The accepted proposals and audit observations in
`docs/pre_editor/` remain immutable provenance. C0/V0 and the active phase contract
still control each change.

## Layout and readable control flow

Use four spaces, no tabs, Allman braces, and a 100-column target. A justified line
up to approximately 120 columns is preferable to a contorted expression. Preserve
indivisible diagnostic strings, raw shader text and meaningful comments even when
they exceed that guidance; record an exception in the affected review. The column
limit is guidance, not a reason to change literal contents or invent abstractions.

Keep one significant action per statement and one statement per line. Write
conditions and error propagation so that acquisition, validation, mutation,
publication and cleanup can be followed separately. Use named intermediate results
when they explain a value or an ownership transition. Split a function at coherent
responsibilities; do not introduce a manager chain or template merely to shorten it.
The formatter cannot make these semantic decisions.

Keep namespace contents indented and class access labels aligned with the class
brace. Use type-attached pointers and references (`Texture*`, `const Handle&`).
Preserve local naming conventions and existing public spellings. Prefer names
that distinguish owners, borrowed views, handles and publication/frame access.
Renames, helper extraction and control-flow changes require their own semantic
scope; they are not part of mechanical formatting.

## Includes, errors and contracts

Use direct, minimal includes and keep implementation dependencies private. Preserve
dependency-sensitive order, including precompiled-header position. Do not sort
includes or using declarations automatically. Avoid global using-directives in new
or migrated normal headers; repair existing pollution when its owning surface is
in scope. Never sweep unrelated headers for consistency.

Recoverable operations return typed `std::expected` results with useful operation
and cause context. Keep failure checks readable and cleanup deterministic. Use
assertions for invariants. Do not add explicit `throw`, `try`, `catch`, exception
transport or custom recoverable exception APIs to GEngine-owned production.
Runtime logging uses the engine abstraction; direct `std::print`/`std::println`
belongs only in appropriate tools, tests or bootstrap code.

Retain comments explaining ownership, lifetime, thread affinity, failure guarantees,
invariants or non-obvious cost. Remove narration or dead experiments only when the
active semantic scope owns that code. Keep `constexpr` and `noexcept` accurate.
Use concepts/requires for meaningful generic eligibility and retain layout/ABI
`static_assert`s; no ornamental templates or modernization sweep.

Normal consumer APIs expose semantic engine contracts. Preserve native isolation
and RBS's existing ImGui widget exception. GPU/context work and destruction remain
on the context thread. Reuse established registries, typed identities, immutable
exact-version frames and the single Physics scheduler. Convenience must not hide
per-frame compilation, decoding, publication or heavy copies. Formatting does not
relax any C0 invariant or prove behavior unchanged.

## Scoped adoption

The exact initial A touchpoints are:

| Path | Initial readability concern | Classification |
| --- | --- | --- |
| `GEngine/src/Renderer/SceneRenderResources.cpp` | Resource construction/publication, typed failure propagation | MUST_FIX_BEFORE_A |
| `GEngine/include/GEngine/Renderer/SceneRenderResources.h` | Resource-facing declarations, ownership and lifetime comments | MUST_FIX_BEFORE_A |
| `GEngine/src/Managers/AssetsManager.cpp` | Asset acquisition/publication and cleanup readability | MUST_FIX_BEFORE_A |
| `RigidBodySimulation/src/RigidBodySimulation.cpp` | Consumer setup and explicit expected-result checks | MUST_FIX_BEFORE_A |

Phase 01 changes only this policy and the root formatter configuration. Phase 02
owns mechanical formatting of the selected initial A files; it must declare its
exact members within its own budget before editing. Phase 03 separately owns
bounded semantic readability work in resource construction and RBS setup. The
table identifies the initial review set, not a blanket edit authorization.
CQ-01 stays open until the required Phase 02/03 work and evidence are accepted.

Other GEngine-owned code is FIX_WHEN_TOUCHED: apply this policy to the functions and
contracts being changed, without unrelated cleanup. Frozen legacy, third-party or
vendored code, generated files, and unrelated Rendering/Physics work are DEFER.
Do not format `external/`, `GEngine/include/external/` or generated outputs as part
of this policy. CQ-02 is an ongoing review obligation on migrated surfaces; policy
adoption does not claim existing code is clean.

Keep policy adoption, mechanical changes, semantic cleanup, API redesign and
performance work separately reviewable. No repository-wide command, recursive
glob, format-on-save rollout, build hook or CI formatting sweep is introduced.

## Pinned formatter and bounded use

Use **clang-format 19.1.5** with the repository-root `.clang-format`. The validated
Windows executable is the existing Visual Studio 2022 x64 tool:

```text
C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin/clang-format.exe
SHA-256: 6161164dc30f7005afbdea1830506f3b80ba2c07ff81af1235d5afd89a375296
```

Check `--version` before use. A different binary distribution or version requires
recorded compatibility and deterministic preview evidence before use; never
silently substitute the formatter on PATH. Updating this pin is a bounded policy
change. It does not change the approved C++23 compiler gate, MSVC 14.44.35207 /
VS2022 v143 / SDK 10.0.26100.0, Premake, dependencies, or Debug MTd / Release MT.

From the verified worktree root, this PowerShell example only prints a preview:

```powershell
$formatter = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin/clang-format.exe'
& $formatter --version
& $formatter --style=file:.clang-format --fallback-style=none --Werror --dump-config
& $formatter --style=file:.clang-format --fallback-style=none --Werror --fail-on-incomplete-format GEngine/src/Renderer/SceneRenderResources.cpp
```

Without `-i`, formatted output goes to stdout. See the LLVM 19
[command documentation](https://releases.llvm.org/19.1.0/tools/clang/docs/ClangFormat.html)
and [style options](https://releases.llvm.org/19.1.0/tools/clang/docs/ClangFormatStyleOptions.html).
Capture previews only under the active worktree's phase evidence root after
checking `docs/pre_editor/active/OUTPUT_PATHS.json`. Select explicit files or ranges;
do not modify source during Phase 01.

The configuration disables automatic changes to include/using order, comment text,
string literals, qualifier order, braces, parentheses and semicolons. It supplies
layout rather than syntax repair. Later mechanical work still needs lexical/token
and diff review, including macros, raw strings and diagnostics, plus affected
compilation under V0. Separate semantic changes need focused evidence for their
actual invariants. No whitespace-only tests or application runs are required for
policy adoption.
