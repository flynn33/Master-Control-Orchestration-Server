# PHASE-13 — Win2D / Direct2D rendering in the WinUI Shell

## Status

- Decided: yes. Operator-authorized GPU rendering for the entire project.
- Scheduled: pending operator priority. Reserved phase-id slot.
- Runs in parallel with PHASE-12 (gateway substrate). Independent.

## Mission

Move the WinUI Shell from XAML's automatic Composition rendering to **explicit Direct2D / Win2D drawing for high-rate live visuals** — per-instance telemetry charts at 60Hz, the Tron grid backdrop as a procedural shader, the activity stream as a flowing visual log. Today's shell already runs on DirectX 11 via Windows Composition (XAML is GPU-composited automatically), but the high-frequency live data surfaces feel slow because they're rendered through XAML primitives that re-layout on every snapshot tick. PHASE-13 cuts those surfaces out of the layout pass and runs them as direct draw operations.

## What we're already getting from XAML+Composition (clarification)

WinUI 3 + Windows Composition gives us:

- GPU compositing of every visual tree element (the "Visual" tree under `ContentDialog` / `Frame` / etc. is a DirectX 11 compositor scene).
- Hardware-accelerated transforms, opacity, clipping, blur effects.
- Smooth 60Hz animation primitives via `CompositionPropertySet` + `ExpressionAnimation`.
- Vsync-locked frame pacing on the system compositor.

PHASE-13 is **not** about getting GPU rendering — we already have it. It's about replacing high-frequency visual surfaces (the ones currently rendered as text + bars in the XAML tree) with **direct GPU-rendered surfaces** that don't trigger XAML re-layout and don't carry the visual-tree element overhead.

## Non-negotiables

- WinUI 3 stays the host. PHASE-13 doesn't replace XAML; it slots Direct2D-rendered `SwapChainPanel` (or Win2D `CanvasControl`) elements *inside* the existing XAML tree.
- Win2D is the preferred wrapper. Microsoft's `Microsoft.Graphics.Win2D` NuGet package wraps Direct2D + DirectX 11 with a managed-friendly API. Same DX11 underneath.
- For surfaces where Win2D's API isn't enough (procedural shaders, custom HLSL), `SwapChainPanel` + raw D3D11 + D2D1 is the fallback.
- The shell's existing XAML surfaces stay XAML. PHASE-13 only converts the four surfaces below.
- `.claude/rules/10-windows-native-cpp.md` Windows-native rule satisfied: Direct2D + Win2D are first-party Microsoft APIs, no third-party deps.

## Scope

### Surfaces converting to Win2D

| Surface | Today (XAML) | PHASE-13 (Win2D) | Why |
|---|---|---|---|
| **Per-instance telemetry charts** (Pools deck) | Browser dashboard already has Canvas sparklines via v0.6.8. Shell shows the same data as text in a `<TextBlock>`. | `CanvasControl` per pool card showing CPU + RAM time series at 60Hz. Same data source (`/api/pools` snapshot's `instances[].telemetry`). | The shell is supposed to be the operator's primary surface; the telemetry charts should be at least as good as the browser. |
| **Tron grid backdrop** | CSS gradient + animated `<svg>` lines in browser; static `<Border>` background in shell. | Procedural shader (HLSL fragment) rendering the grid + perspective lines + scan-line glow. Vsync-locked, sub-millisecond cost per frame. | Operator has stated visual identity is part of the product. Procedural is faster + sharper than rasterized SVG, and resolution-independent on multi-DPI displays. |
| **Activity stream** | XAML `ItemsControl` rebuilding the visual tree on every snapshot tick. | `SwapChainPanel` drawing the stream as a scrolling flowing log of events, with severity color-coding and 60Hz scroll. | The current rebuild is the "feels sluggish" complaint. Avoiding the layout pass = instant. |
| **Pool saturation gauges** | `<ProgressBar>` per pool. | Animated rings (Win2D drawing primitives) with smooth angular interpolation. | A small upgrade but a visible one; ties together with the per-instance charts on the same card. |

### Surfaces staying XAML

- Settings forms (text boxes, toggles, the Apply button).
- Navigation pane.
- Hero header / status chip.
- Firewall guidance + onboarding instructions.
- Anything that's a static layout with primarily text content.

These all benefit from XAML's accessibility / IME / focus behavior; switching them to Direct2D would lose all that for no perf gain.

### Out of scope (later phases)

- Browser dashboard rewrite. PHASE-13 is shell only. Browser stays Canvas-based (already adequate at the sparkline level).
- Multi-monitor / multi-DPI per-monitor scaling beyond what XAML handles (Win2D handles it natively, but operator-configurable monitor selection is not in scope).
- 3D / AR visualizations. Strictly 2D + procedural backgrounds.

## File-by-file plan

| File | Action | Why |
|---|---|---|
| `src/MasterControlShell/packages.config` | Edit | Add `Microsoft.Graphics.Win2D` v1.x. |
| `src/MasterControlShell/MasterControlShell.vcxproj` | Edit | Reference the Win2D package; link `windowsapp.lib` already present. |
| `src/MasterControlShell/PoolsCardsSectionControl.xaml` | Create (or extend `RuntimeSectionControl.xaml`) | Add `<canvas:CanvasControl>` per pool card for the telemetry chart. |
| `src/MasterControlShell/PoolsCardsSectionControl.xaml.cpp` | Create | `Draw` event handler that pulls the latest snapshot and renders the chart via Win2D drawing primitives. |
| `src/MasterControlShell/TronBackdropPanel.xaml` | Create | `<canvas:CanvasAnimatedControl>` for the procedural grid. |
| `src/MasterControlShell/TronBackdropPanel.xaml.cpp` | Create | HLSL effect compilation + per-frame draw of the grid + scan-line. |
| `src/MasterControlShell/ActivityStreamSurface.xaml` | Create | `<SwapChainPanel>` for the scrolling activity log. |
| `src/MasterControlShell/ActivityStreamSurface.xaml.cpp` | Create | Direct2D draw loop with WriteUtf8 for severity-colored text. Subscribes to the runtime's activity ring via `ShellRuntime::FetchActivityEvents` (already exists). |
| `src/MasterControlShell/MainWindow.xaml` | Edit | Insert `TronBackdropPanel` as the deepest layer. Replace existing pool-instance text rows with `PoolsCardsSectionControl`. Insert `ActivityStreamSurface` where the activity feed currently lives. |
| `docs/wiki/Tron-UI-Theme.md` | Edit | New "PHASE-13 GPU rendering" section explaining the architecture + perf characteristics. |
| `docs/wiki/Versions.md` | Edit | Add PHASE-13 to the phase table. |
| `handoff/realignment/PHASE-13-completion-report.md` | Create | Required per `.claude/rules/40-validation-reporting.md`. |

## Acceptance criteria

1. **Telemetry charts at 60Hz.** Operator opens the Pools deck on a host with active pools; CPU% and RAM MB sparklines update at vsync, no stutter, no visual-tree rebuild on snapshot ticks.
2. **Tron backdrop is procedural.** Resizing the window doesn't trigger SVG re-rasterization; the grid stays sharp at any DPI / resolution.
3. **Activity stream scrolls smoothly.** New events appear with smooth fade/scroll instead of an instantaneous snap-into-place.
4. **Saturation rings animate.** `atSaturation` flag transition produces a smooth color + angle interpolation on the ring rather than an instantaneous swap.
5. **No regression on text surfaces.** Settings forms, navigation, headers all behave identically to v0.6.x.
6. **Memory cost.** Total private working set increase < 80 MB in steady state. Win2D loads its DX11 runtime once at shell start; per-canvas overhead is small.
7. **Shutdown clean.** Closing the shell releases all DX11 / D2D resources without warnings in the debug heap.

## Effort estimate

- Per-instance telemetry charts (the biggest visible win): 2-3 days.
- Tron backdrop with procedural shader: 1-2 days (one HLSL fragment, plus the Win2D plumbing).
- Activity stream `SwapChainPanel`: 3-5 days (manual text layout + scroll animation is non-trivial).
- Saturation rings: 1 day.
- Total: ~2 weeks of focused engineering.

Lands one-surface-at-a-time. The first commit can ship just the telemetry charts; subsequent commits add the others. Each is independently usable.

## Risks

| Risk | Mitigation |
|---|---|
| Win2D version conflict with WinUI 3 1.5 SDK in `Microsoft.WindowsAppSDK 1.5.240227000`. | Pin Win2D to a version matrix Microsoft documents as compatible; verify before merging. |
| HLSL shader compilation cost on first frame. | Compile at shell startup, cache the compiled blob to `%LOCALAPPDATA%\Master Control Orchestration Server\shaders\` for subsequent runs. |
| `SwapChainPanel` interop bugs with XAML focus management. | Activity stream is read-only — no focus needed. |
| Multi-monitor DPI changes mid-session. | Win2D handles `WindowDpiChanged` events natively; just need to subscribe. |

## Dependencies

- v0.6.8 per-instance telemetry data (already in this release) — feeds the chart surfaces.
- `MainWindow`'s existing 2-second live tick — drives chart sample appends. Same cadence as v0.6.5.
- `ShellRuntime::FetchActivityEvents` (already exists) — feeds the activity stream surface.
- No PHASE-12 dependency. PHASE-13 can ship before, after, or alongside PHASE-12.

## Cross-references

- [Tron UI theme](../../docs/wiki/Tron-UI-Theme.md)
- [v0.6.8 per-instance Canvas charts in browser](../../resources/web/app.js) — the conceptual sibling. PHASE-13 brings the same telemetry visualization to the shell at higher fidelity.
- [Microsoft Win2D docs](https://microsoft.github.io/Win2D/WinUI3/html/Introduction.htm)
- [Microsoft DirectX 11 graphics docs](https://learn.microsoft.com/en-us/windows/win32/direct3d11/)
