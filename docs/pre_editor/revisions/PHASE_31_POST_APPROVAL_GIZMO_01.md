# Phase 31 post-approval correction 01 — numeric Gizmo

This correction is based on approved commit `244e2c1aa4f97a87e5e1cffdc3660a7ecfc38100`. **The corrected Release x64 candidate is ready for focused Owner inspection.** The original Phase 31 approval and the manual result for this correction have separate records.

## Keyboard input

The pinned ImGui text widget consumes the current character queue on navigation activation and skips insertion. The previous overlay tried to compensate by copying part of that queue into an empty, error-free buffer. Its copy and ImGui's numeric filter handled activation differently. For example, refocusing after invalid input could consume the next digit without inserting it.

The overlay now registers one field and requests direct keyboard focus on that item. ImGui's numeric filter consumes SDL text on the first and subsequent frames. This also handles tool or axis letters arriving in the same batch as a number. The existing Transform session still owns the starting pose, preview, placement validation, commit, cancellation and history. The sole production edit is in [RbsViewportToolbar.cpp](../../../RigidBodySimulation/src/RbsViewportToolbar.cpp).

## Overlay

The preferred width is 344 logical pixels at 100% UI scale. The value uses a 25% larger font and a taller input field. The axis accent identifies the operation and outlines the focused field. The title gives the operation, axis and unit. Enter and Escape hints share the footer with validation feedback. The overlay fits the available viewport and uses the captured pivot to choose its corner.

## Verification

The first ordinary Release x64 toolbar compile and RBS link passed in 15.641 guarded seconds with zero warning or error diagnostics. A subsequent geometry check found that the upper position could overflow a short viewport when the selected pivot was offscreen. At 320 × 180 and 200% scale, the calculated lower edge was 233.072. The final source clamps it to 172.8. The first build predates that one-line clamp. [First build](../../../logs/pre_editor/phase-31/post-approval-gizmo-01/focused-Release/result.json)

The Owner authorized one final toolbar compile and link after the clamp. It passed in **15.126 of 20 guarded seconds** with zero compiler or linker warnings or errors. The ordinary executable at `bin/Release/RigidBodySimulation/RigidBodySimulation.exe` has SHA-256 `d4aea32c3ef1183442aaf0094cc50e0958ff56710ed02d6fa318e73fa0826c4f`. The first binary and its pre-clamp evidence are retained separately. [Final build](../../../logs/pre_editor/phase-31/post-approval-gizmo-01/final-Release/result.json) · [Corrective candidate status](../../../logs/pre_editor/phase-31/post-approval-gizmo-01/status-final.json)

Offline checks covered input split across frames, same-frame confirm, signed decimals, uniform and axis scale, cancellation and bounded layout. These are source and mathematical checks. The Owner will verify the real keyboard and display. The approved executable was retained byte-for-byte at its original SHA-256 `9d5d56a6657cdf0bd96faf8d6854d139fc4f099443e5c67a78676253ca1b1e53`. [Offline evidence](../../../logs/pre_editor/phase-31/post-approval-gizmo-01/offline-checks.json) · [Scope and original checkpoint](../../../logs/pre_editor/phase-31/post-approval-gizmo-01/scope.json)

## Focused Owner check

1. In Edit mode, select an editable box and activate its viewport. Use World orientation and clear space. Type **T X 5 Enter**, **R Y 90 Enter**, then **R Z -45 Enter**, with each key pressed once. Undo between examples to inspect each relative change from the same pose.
2. Type **T X -0.25**, check the preview, and press **Escape**. Try **S 2 Enter**, then **Ctrl+Z** and **Ctrl+Y**. Confirm the completed scaling operation occupies one history entry.
3. Click an axis handle and type a value. Inspect focus, axis color, units and hints at your usual DPI and in a narrow viewport. Commit and cancel should each dismiss the overlay.
