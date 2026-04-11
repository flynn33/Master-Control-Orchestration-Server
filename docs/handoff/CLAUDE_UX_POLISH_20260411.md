# Claude UX Polish Handoff — 2026-04-11

## Project Identifier
Master Control Orchestration Server (Forsetti-compliant Windows orchestration control plane for Windows 11 and Windows Server 2022 Desktop Experience, Tron-inspired, personal-use product).

## Current Objective
Quality-and-rework pass covering four operator complaints:
1. Install/uninstall UX polish.
2. WinUI shell + browser dashboard UI/UX polish.
3. Runtime bug sweep / stabilization.
4. Branding unification under "Master Control Orchestration Server".

Execution plan: `C:\Users\master-control.MCP-GATEWAY\.claude\plans\piped-cuddling-sloth.md`.

## Active Task
Phase A stabilization + Phase C1 (installer theming) + Phase D1a/D2/D3 (shell + browser polish). Phase B (internal service rename) was investigated and deferred. Phase C2–C6 and Phase E validation have not started.

## Completed Work This Pass
### Phase A — Stabilization sweep
- Grepped `src/` and `include/` (excluding third-party `packages/`) for `TODO|FIXME|HACK|XXX|BUG|not implemented`. **Zero first-party matches.**
- Re-read `src/MasterControlBootstrapper/setup_main.cpp` end-to-end. Confirmed two items the plan flagged as broken are actually correct:
  - Elevation (`launchProcess`, lines ~1100–1205) already uses `ShellExecuteExW` with the `runas` verb and a plain `lpParameters` string. No Base64 relay exists in the C++ bootstrapper. The Base64 relay lives only in `scripts/Package-MasterControlOrchestrationServer.ps1` as a fallback path.
  - `maybeLaunchShell` (lines ~1218–1259) is fully wired: prompt, launch via `ShellExecuteW`, result code capture, structured log.
- No fixes needed from the Phase A task list.

### Phase B — Branding decision
- Audited every `MasterControlProgram` reference: all hits are the internal SCM service key, uninstall registry key, legacy data/share dir leaf, and legacy-detection fallbacks. These are **invisible to users** and intentionally kept for upgrade compatibility with pre-0.2 installs.
- User-visible strings (SCM display name at `main.cpp:1664`, Programs & Features, shortcut names, firewall rule names at `main.cpp:42-43`, `MainWindow.xaml` title/eyebrow) already resolve to "Master Control Orchestration Server".
- **Deferred the internal rename.** No code changes in this pass. If a future clean break is wanted, the migration strategy is documented in the plan file (introduce new service key, detect and migrate legacy, shrink legacy layer after one release). Not done now because it introduces upgrade risk without visible benefit.

### Phase C1 — Tron-themed installer progress window
- `src/MasterControlBootstrapper/setup_main.cpp`: reworked `ProgressWindow` struct, `progressWindowProc`, `createProgressWindow`, and `destroyProgressWindow`.
- Added `kTronBackgroundColor` (#060A10), `kTronAccentColor` (#00F6FF), `kTronTextPrimaryColor` (#E6FCFF), `kTronTextMutedColor` (#8CB7C4) constants that mirror `App.xaml`.
- `WM_ERASEBKGND` paints the client in dark blue-black and draws a 2px cyan accent bar along the top edge (matches the shell hero card edge).
- `WM_CTLCOLORSTATIC` paints the eyebrow (control id 1) in accent cyan, the stage label (id 3) in muted cyan, and the hero title (id 2) in primary text. All other statics fall through to primary text.
- Introduced `createTronFont(height, weight)` wrapping `CreateFontW` with `Bahnschrift SemiCondensed`, `CLEARTYPE_QUALITY`, and `FF_SWISS`. Three font instances are cached on the struct: eyebrow (-12, SemiBold), header (-22, SemiBold), body (-13, Normal). All destroyed in `destroyProgressWindow`.
- Progress bar switched to `PBS_SMOOTH` with `PBM_SETBARCOLOR`/`PBM_SETBKCOLOR` set to accent cyan / background, overriding the default Explorer green.
- Window title changed to `MASTER CONTROL ORCHESTRATION SERVER · SETUP` (all caps, middle dot separator).
- Window size bumped from 520×190 to 560×240 to accommodate the eyebrow + hero title + two-line status layout.
- GDI resources are owned by `ProgressWindow` and freed in `destroyProgressWindow` — no leaks.

### Phase D1a — Shell resource dictionary polish
- `src/MasterControlShell/App.xaml`: appended new resources before `ShellListViewItemStyle`:
  - Brushes: `ShellFocusBrush` (#CC00F6FF), `ShellPressFillBrush` (#2200F6FF), `ShellSkeletonBrush` (#22FFFFFF), `ShellSuccessSoftBrush`, `ShellWarningSoftBrush`, `ShellDangerSoftBrush` (all at 33 alpha for button tinting).
  - Styles: `ShellStatusChipStyle` (pill, 999 corner radius, accent border, soft fill), `ShellStatusChipTextStyle` (11pt Bahnschrift SemiBold, 120 character spacing).
  - Button variants based on `ShellCommandButtonStyle`: `ShellSuccessButtonStyle`, `ShellWarningButtonStyle`, `ShellDangerButtonStyle`.
  - Metric typography: `ShellMetricLabelTextStyle` (10pt, 140 spacing, ellipsis trim) and `ShellMetricValueTextStyle` (24pt).
  - `ShellSkeletonBlockStyle` for loading placeholders.
- All new resources are additive. No existing styles changed.

### Phase D2 — OverviewSectionControl rework
- `src/MasterControlShell/OverviewSectionControl.xaml`: replaced the 50-line stub with a hero card + operational snapshot card + two-column narrative grid + authored-surfaces legend. Uses new eyebrow labels ("MASTER CONTROL · OVERVIEW", "OPERATIONAL SNAPSHOT", "HOST & NETWORK", "RUNTIME CONFIGURATION", "AUTHORED SURFACES") and the new `ShellStatusChipStyle`.
- `src/MasterControlShell/OverviewSectionControl.xaml.cpp::ApplySnapshot`: status chip text changed to short uppercase forms to match chip styling:
  - Online: `ADMIN API ONLINE · SYNCHRONIZED`
  - Offline: `ADMIN API OFFLINE · CACHED STATE`
- The four bound text fields (`OverviewTextBlock`, `OverviewStatusText`, `EnvironmentNarrativeText`, `ConfigurationNarrativeText`) kept the same x:Names, so no other C++ or header changes were needed.

### Phase D3 — Browser dashboard polish layer
- `resources/web/styles.css`: appended a trailing "Tron polish layer" block with:
  - `@keyframes tron-accent-pulse` (2.8s ease-in-out infinite) — cyan box-shadow breathing.
  - `.badge[data-tone="info"], #healthBadge[data-tone="info"]` — applies the pulse to the live health indicator while connecting.
  - `:focus-visible` outline (2px cyan + matching glow) on buttons, links, role=button, and dialogs.
  - `<dialog>::backdrop` with a radial-gradient and `backdrop-filter: blur(4px)` — gives overlay/danger dialogs a true Tron backdrop instead of the default semi-opaque gray.
  - `@media (prefers-reduced-motion: reduce)` — neutralizes animations/transitions and disables the button hover lift for motion-sensitive users.
- No changes to `app.js` or `index.html`. Confirmed both dialogs are already wired (`app.js` has `showModal`, `close` listeners, workspace destinations). The plan's earlier claim that the overlay was "empty / not wired" was based on an incorrect exploration summary.

## Design Decisions
- **Keep legacy SCM identity.** The internal `MasterControlProgram` service key stays. Rationale above.
- **Extend `App.xaml` rather than fork to a separate `TronPalette.xaml`.** A separate dictionary adds file overhead without localized benefit since `App.xaml` is already the canonical palette and every section references it. If an out-of-process consumer (e.g. a test harness) ever needs the palette, it can be extracted later.
- **Installer theming via Win32 owner-drawn approach (not a rewrite to XAML Islands).** Keeping the launcher as a small plain Win32 executable preserves its speed and avoids pulling the Windows App SDK into the setup path. All theming is done with standard `CreateSolidBrush` + `CreateFontW` + `WM_CTLCOLORSTATIC` + `WM_ERASEBKGND`.
- **Status chip text is short and uppercase.** Existing prose copy ("Admin API reachable. The desktop shell is synchronized with...") was unsuitable for a 10-char pill. Moved the prose into `OverviewTextBlock` if needed later; kept the chip terse.

## Constraints
- No local Windows build environment in this session. **None of these edits have been compiled.** A build-and-run pass on a Windows workstation is required before release.
- `MasterControlRuntime.cpp` is 381 KB (~6000+ lines); full inspection was out of scope for this polish pass.
- The v0.1.65/v0.1.66 custom title-bar drag fixes were not re-verified against a packaged shell build.

## Files Touched
- `src/MasterControlBootstrapper/setup_main.cpp` — ProgressWindow rewrite + Tron palette constants + `createTronFont` helper + new `progressWindowProc` cases.
- `src/MasterControlShell/App.xaml` — appended polish brushes, chip styles, tonal button variants, metric typography, skeleton style.
- `src/MasterControlShell/OverviewSectionControl.xaml` — full visual rework.
- `src/MasterControlShell/OverviewSectionControl.xaml.cpp` — status chip text change.
- `resources/web/styles.css` — appended Tron polish layer (reduced-motion, focus-visible, accent pulse, dialog backdrop).
- `CHANGELOG.md` — `[Unreleased]` bullets for each change above.

## Validation Results
- **None.** No builds run, no automated tests executed. Edits are source-only.
- Recommended local validation sequence once a build environment is available:
  1. `cmake --build` the bootstrapper target and visually confirm `MasterControlOrchestrationServerSetup.exe` renders the new Tron progress window against a staged payload.
  2. `cmake --build` the shell target and confirm `App.xaml` still parses — any unresolved `StaticResource` references would surface as a XAML compile error.
  3. Run the shell and navigate to Overview. Verify the new hero card renders correctly, the status chip populates with the short uppercase text, and the narrative cards still display the bound data.
  4. Serve `resources/web/` via the local browser API host. Verify focus-visible outlines, badge pulse on the "Connecting" health badge, dialog backdrop blur, and reduced-motion behavior with `prefers-reduced-motion: reduce` simulated in dev tools.
  5. Run `IDE_ACCEPTANCE_MIXED` and `IDE_ACCEPTANCE_MANAGED` on Windows 11 to confirm no install regression.
  6. Re-run on Windows Server 2022 Desktop Experience (Phase E — still outstanding).

## Next Steps / Follow-Up Work
Still outstanding from the execution plan:
- **Phase C2** — Unified log dashboard (pointer file on desktop, structured logs in PublicDocuments, `--open-logs` flag).
- **Phase C3** — Harden elevation fallback in `scripts/Package-MasterControlOrchestrationServer.ps1` (the remaining Base64 relay).
- **Phase C4** — Upgrade/repair/uninstall transaction journal + rollback.
- **Phase C5** — WiX MSI evaluation (time-boxed; decide go/no-go).
- **Phase C6** — Windows Server 2022 compatibility sweep (Desktop Experience detection, service path differences).
- **Phase D (shell)** — The remaining 9 section XAML files still need consistency pass for eyebrow + hero pattern, loading/empty states, animations, error dialogs.
- **Phase D (wizards)** — Audit the 7 "GUIDED SETUP WIZARDS" buttons in `MainWindow.xaml` for real end-to-end flows.
- **Phase E** — Acceptance matrix validation, 24h soak, doc refresh, v0.2.0 tag.
