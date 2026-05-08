# MCOS Agent Decision Log

Drift-resistant memory anchor for the Claude agent driving MCOS development.
Append a new entry per session/release. Keep entries terse — each is a delta,
not a recap. The git log + `VERSION.json` history is the canonical record;
this file is the agent's working memory of *why* and *what's next*.

## v0.8.1 (in progress) — Tron-red palette + cross-section drag + grid backdrop

Operator directives:
1. Stop punting subtasks as "out of scope" if they materially advance the goal.
2. Switch app accent from cyan to a Tron-styled red (CLU palette).
3. Replace the curved decorative background — it isn't Tron-styled.
4. Make cross-section drag actually work on the Telemetry tab.
5. Spin up MCP memory infrastructure to prevent agent drift.

Decisions:
- Color mapping (cyan -> Tron CLU red-orange):
  - `ShellAccentBrush`: `#00FFFF` -> `#FF3D2E` (CLU red-orange primary).
  - `ShellTileEdgeBrush`: `#6600FFFF` -> `#66FF3D2E` (40% alpha edge).
  - `ShellAccentSoftBrush` / `ShellGlowBrush` / `ShellGridlineBrush`: keep
    structure, swap base hue to red. Soft variants drop alpha to 0x33-0x4D.
  - Inline `#00DFFF` / `#1000DFFF` / `#2200DFFF` / `#1400DFFF` / `#F0xxxxxx`
    cyan washes throughout MainWindow.xaml -> red equivalents.
  - Status semantic colors (good=green-mint #1cf2c1 / warn=amber #ffc857 /
    crit=red #ff6a80 / neutral=#8cb7c4) are NOT changed -- they convey state,
    not chrome. Critical red stays distinct from chrome accent because the
    chrome is orange-red while critical is pink-red.
- Background:
  - Two big decorative `<Ellipse>` strokes are removed (those are the
    "curves" the operator called out).
  - Replace with a proper Tron grid: two perpendicular sets of perspective
    lines drawn into the same `<Canvas>`, glow-tinted in the new red.
- Cross-section drag:
  - Drop the section-grid-restriction. Source tile reparents to the target's
    section grid, takes target's position, target moves to source's old
    position (and old parent if cross-section). Layout state captures the
    new section IDs and persists.
- MCP memory:
  - No off-the-shelf memory connector in the registry. Using this file as
    a session-durable, git-versioned decision log instead. Update on every
    major decision so a future compaction can rehydrate context from here
    plus VERSION.json plus the git log.

## v0.8.0 — Operator-customizable Telemetry tab (shipped)

- 15 tiles each gained x:Name / Tag / drag-drop handlers / detach + hide buttons.
- Customize Layout flyout with per-tile checkboxes.
- Detach to desktop window via `Microsoft::UI::Xaml::Window`. Reattach via
  the in-window button or window-close.
- Layout persists to `%ProgramData%\MasterControlOrchestrationServer\config\telemetry-layout.json`.
- Cross-section drag was rejected at the drop handler — explicitly punted to
  v0.8.1. Operator pushed back; v0.8.1 makes it work.

## v0.7.x history

Fully captured in `VERSION.json` history array. v0.7.5 (uninstaller fix), v0.7.6
(sub-agent telemetry visualization), v0.7.7 (bind-address LAN-resolution),
v0.7.8 (SUB-AGENT GRID footer per-badge telemetry), v0.7.9 (Telemetry-tab
cleanup + status dots).
