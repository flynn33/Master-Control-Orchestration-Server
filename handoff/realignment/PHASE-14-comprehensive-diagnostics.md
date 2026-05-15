# PHASE-14 — Comprehensive Diagnostics Module

## Status

- Decided: yes. Operator-authorized 2026-05-10.
- Scheduled: this is the next-step phase after PHASE-11. Independent of PHASE-12 / PHASE-13.
- Approval required before MCOS source edits: **yes** (per `.claude/rules/40-validation-reporting.md` and CLAUDE.md "Required execution behavior" §6).

## Mission

Replace the disparate today-state — per-component `events.jsonl` writes, in-memory `SelfTestSnapshot`, narrow `/api/diagnostics/runtime-stats`, scattered HRESULT logging — with a **single Diagnostics Service** that:

1. Owns a process-wide event aggregate (in-memory ring + SQLite-backed persistent store).
2. Receives every error / warning / informational event from every MCOS subsystem (runtime, gateway, supervisor, lease router, discovery, governance, http, pool, setup, external).
3. Receives the boot self-test snapshot from `runBootSelfTestsAsync` so test results survive past process exit.
4. Exposes a queryable HTTP API for the dashboard, the shell, and the `mcos-bridge` MCP tool surface.
5. Renders Markdown and JSON exports on demand.
6. Surfaces in the WinUI Shell behind a new `DiagnosticsSectionControl` with **Export Markdown** and **Export JSON** buttons that open the native Windows file picker (`Windows::Storage::Pickers::FileSavePicker`) and write the chosen file.
7. Surfaces in the browser dashboard with a Diagnostics tab and a corresponding Export button (browser download).

After PHASE-14, every error, warning, and notable event MCOS produces is recorded in one place, addressable by Claude Code (via the MCP plugin) and by the operator (via the shell or browser). Exports give the operator a portable snapshot for off-host triage.

## Why

The realignment program (PHASE-00 through PHASE-11) leaves the runtime healthy, but the diagnostic surface is fragmented:

- `MasterControlDiagnostics.h` writes per-component `events.jsonl` files under `%PUBLIC%\Documents\Master Control Orchestration Server\logs\<component>\`. **Only `MasterControlRuntime.cpp` calls it today.** Every other subsystem either uses `OutputDebugStringW`, swallows the error, or returns it without reporting.
- `runBootSelfTestsAsync()` produces a `SelfTestSnapshot` but holds it only in memory under `selfTestMutex_`. A failed self-test is invisible to operators after the next restart.
- `/api/diagnostics/runtime-stats` returns a point-in-time bucket view of install state. There is no time-series, no historical query, no severity facet, no export.
- The operator has no way to grab a complete diagnostic snapshot to attach to a support thread without manually concatenating files from `logs/` subdirectories on disk.

The user-stated requirement is one centralized module, queryable by both Claude Code and the operator, with a UI Export button that opens the native file picker. That is exactly what this phase delivers.

## Non-negotiables

- **Windows-native** per `.claude/rules/10-windows-native-cpp.md`. Persistent store: SQLite (already in tree via `mcp-server-sqlite-npx` operator surface; we link the C library, not the npx wrapper). File picker: `Windows::Storage::Pickers::FileSavePicker` (WinUI 3 / WinRT). HTTP routes: existing in-process listener (no new framework).
- **No fake telemetry** per `.claude/rules/00-mcos-realignment.md`. If the SQLite store is unreachable (locked, disk full), `DiagnosticsService::report` returns a structured failure rather than silently dropping or pretending to succeed. The in-memory ring still accepts the event so the runtime keeps working.
- **Forsetti vendoring untouched** per `.claude/rules/20-forsetti-clu-governance.md`.
- **No version bump** until PHASE-10 packaging cycle revisits this.
- **Backward compatible.** The existing `appendEvent` / `appendTelemetry` inline functions keep their signature and continue to write the per-component `events.jsonl`. They additionally call into `DiagnosticsService::report` so callers that haven't been migrated keep producing data into the central store. Migration of individual call sites can happen incrementally in later phases.
- **Approval gate.** No MCOS source edit until the operator approves the plan in this file (per CLAUDE.md "Required execution behavior" §6).

## File-by-file plan

| # | File | Action | Purpose | LOC est. |
|---|---|---|---|---|
| 1 | `handoff/realignment/PHASE-14-comprehensive-diagnostics.md` | Created (this file) | Phase spec + plan | 250 |
| 2 | `handoff/realignment/manifest.json` | Edit | Add `PHASE-14` entry under `phases[]`. | +30 |
| 3 | `include/MasterControl/DiagnosticsTypes.h` | Create | Public types: `DiagnosticsSeverity` enum (`Debug` / `Info` / `Warning` / `Error` / `Critical`), `DiagnosticsRecord` struct (id, capturedAtUtc, source, severity, eventName, message, data, sessionId, sequence), `DiagnosticsQuery` struct (filters), `DiagnosticsExportFormat` enum (`Markdown` / `Json`). | 90 |
| 4 | `include/MasterControl/DiagnosticsStore.h` | Create | Pure-virtual `IDiagnosticsStore` interface and concrete `SqliteDiagnosticsStore` declaration. Methods: `Insert`, `Query`, `Count`, `Clear`, `LastInsertSequence`. | 60 |
| 5 | `src/MasterControlApp/DiagnosticsStore.cpp` | Create | SQLite implementation. Schema: `diagnostics_events(id INTEGER PK AUTOINCREMENT, captured_at_utc TEXT, source TEXT, severity INTEGER, event_name TEXT, message TEXT, data_json TEXT, session_id TEXT, sequence INTEGER)`. Indexes on `(captured_at_utc, severity)` and `(source, captured_at_utc)`. WAL mode. Schema migration via a `schema_version` table. | 350 |
| 6 | `include/MasterControl/DiagnosticsService.h` | Create | Singleton-style service exposed to the runtime. API: `report(severity, source, eventName, message, data)`, `recordSelfTest(snapshot)`, `query(filters)`, `summary()`, `exportMarkdown()`, `exportJson()`, `clear(reason)`. Owns the in-memory ring (1000 records) and the persistent store. | 80 |
| 7 | `src/MasterControlApp/DiagnosticsService.cpp` | Create | Implementation. Construction takes a path to the SQLite DB. `report` writes to ring + store atomically (lock + emplace + insert). `summary()` returns counts by severity, top sources, latest 5 records. `exportMarkdown()` renders a structured doc with sections per severity. `exportJson()` returns the full record set as a JSON array. | 450 |
| 8 | `include/MasterControl/MasterControlDiagnostics.h` | Edit | Add a thin shim: `appendEvent` and `appendTelemetry` continue writing per-component jsonl AND additionally call `DiagnosticsService::reportFromAppendEvent(...)` if the service is initialized. Use a weak symbol pattern (`std::function` registered at runtime startup) so the header remains lightweight and doesn't pull in the service everywhere. | +40 |
| 9 | `src/MasterControlApp/MasterControlRuntime.cpp` | Edit | (a) At startup, construct `DiagnosticsService` with the DB path under `%PUBLIC%\Documents\Master Control Orchestration Server\diagnostics\diagnostics.db`, register it with the appendEvent shim. (b) After `runBootSelfTestsAsync` completes, call `DiagnosticsService::recordSelfTest`. (c) Register HTTP routes: `GET /api/diagnostics/events`, `GET /api/diagnostics/summary`, `GET /api/diagnostics/self-test`, `GET /api/diagnostics/export?format=markdown\|json`, `POST /api/diagnostics/clear`. | +250 |
| 10 | `src/MasterControlApp/CMakeLists.txt` | Edit | Add `DiagnosticsStore.cpp` and `DiagnosticsService.cpp` to the target. Link `sqlite3` (vcpkg-managed; add to `vcpkg.json` if not already present). | +5 |
| 11 | `vcpkg.json` | Edit (if needed) | Add `"sqlite3"` to dependencies if not already declared. | +1 |
| 12 | `.claude-plugin/mcos-control/mcp-servers/mcos-bridge/server.py` | Edit | Add tools: `mcos_diagnostics_summary`, `mcos_diagnostics_query`, `mcos_diagnostics_self_test`, `mcos_diagnostics_export_markdown`, `mcos_diagnostics_export_json`, `mcos_diagnostics_clear`. Each maps to the corresponding HTTP endpoint with input schema documented. | +200 |
| 13 | `src/MasterControlShell/DiagnosticsSectionControl.xaml` | Create | New shell section: severity filter ComboBox, source filter ComboBox, message search TextBox, results ListView (severity-color-coded), **Export Markdown** + **Export JSON** + **Refresh** + **Clear** buttons, status TextBlock. | 130 |
| 14 | `src/MasterControlShell/DiagnosticsSectionControl.xaml.h` | Create | C++/WinRT header. | 40 |
| 15 | `src/MasterControlShell/DiagnosticsSectionControl.xaml.cpp` | Create | Click handlers. Export buttons: construct `Windows::Storage::Pickers::FileSavePicker`, set suggested file name (`mcos-diagnostics-<timestamp>.md` or `.json`), set file types, await `PickSaveFileAsync`. On result, GET the export URL, write bytes to chosen file. Status text shows `Saved to <full path>`. | 280 |
| 16 | `src/MasterControlShell/MainWindow.xaml` | Edit | Add `Diagnostics` NavigationViewItem. | +5 |
| 17 | `src/MasterControlShell/MainWindow.xaml.cpp` | Edit | Wire the nav item to display the new section control. | +10 |
| 18 | `src/MasterControlShell/CMakeLists.txt` | Edit | Add the three new files to the build. | +6 |
| 19 | `src/MasterControlShell/ShellRuntime.h` | Edit | Add fetch helpers: `FetchDiagnosticsSummary`, `FetchDiagnosticsEvents`, `FetchDiagnosticsExport(format)`, `ClearDiagnostics(reason)`. | +20 |
| 20 | `src/MasterControlShell/ShellRuntime.cpp` | Edit | Implement the four helpers as HTTP calls to the local admin port. | +120 |
| 21 | `resources/web/index.html` | Edit | Add a `Diagnostics` tab button + content section with severity/source filter selects, search input, results table, **Export Markdown** + **Export JSON** + **Refresh** + **Clear** buttons. | +60 |
| 22 | `resources/web/app.js` | Edit | Wire the new tab + Export buttons. Browser export = `fetch` the URL, build a `Blob`, programmatic `<a download="...">` click. (Browsers cannot open the native file picker; the user gets the OS download dialog.) | +180 |
| 23 | `resources/web/styles.css` | Edit | Style the diagnostics table + severity badges. | +60 |
| 24 | `tests/MasterControlOrchestrationServerTests.cpp` | Edit | Add tests: store CRUD, query filters, ring eviction, export Markdown contains every severity section, export JSON parses, HTTP route returns expected envelope, `recordSelfTest` populates the store. | +300 |
| 25 | `handoff/realignment/PHASE-14-completion-report.md` | Create at end | Required per `.claude/rules/40-validation-reporting.md`. | 250 |

**Totals**: 13 new files, 12 edits. ~3,000 lines of new code (~half is tests + UI XAML/JS).

## Acceptance criteria

1. **Persistence.** Stop and restart MCOS; the events table retains every record from the prior session (with their session_id intact for filtering).
2. **Self-test integration.** Boot self-test produces a `recordSelfTest` write into the store. Querying `GET /api/diagnostics/events?source=self-test` returns the latest snapshot's records.
3. **Aggregation.** `appendEvent` calls from all current call sites continue to write to per-component jsonl AND appear in the central store.
4. **HTTP API contracts.** All five new routes return the documented JSON envelope. `GET /api/diagnostics/export?format=markdown` returns `text/markdown` with the rendered document.
5. **MCP plugin.** From within Claude Code, `mcos_diagnostics_summary` returns the same payload as the HTTP endpoint. `mcos_diagnostics_export_markdown` returns the rendered doc as a tool result.
6. **Shell UI.** Operator opens the Diagnostics section in the shell, clicks **Export Markdown**, the Windows file picker opens, operator selects a folder + filename, file lands at the chosen path with the rendered content. Status TextBlock updates with the full path.
7. **Browser UI.** Operator opens the Diagnostics tab, clicks **Export JSON**, the browser's standard download dialog appears with `mcos-diagnostics-<timestamp>.json` as the filename.
8. **No fake state.** If the SQLite DB cannot be opened (path unwritable, disk full), `DiagnosticsService::report` returns a structured `Result` with `ok=false` and a clear reason; the runtime continues without crashing; `summary()` includes a `storeUnavailable` field with the underlying message. The HTTP endpoint surfaces the same field.
9. **Forsetti compliance script** (`scripts/check-mastercontrol-forsetti.ps1`) passes after the changes.
10. **ctest** passes with the new tests.
11. **Validation chain** from CLAUDE.md runs end-to-end:
    ```powershell
    cmake --preset debug
    cmake --build --preset debug
    ctest --preset debug --output-on-failure
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-mastercontrol-forsetti.ps1
    ```

## Effort estimate

- Backend (items 3–11): 2–3 days. Most of the time is the SQLite schema + the HTTP route surface + tests.
- MCP plugin tools (item 12): 0.5 day. Pattern is the same as existing tools in `mcos-bridge/server.py`.
- Shell UI (items 13–20): 2–3 days. WinUI 3 file picker + the new section control.
- Browser UI (items 21–23): 1 day.
- Tests + verification (item 24 + chain): 1 day.

**Total: ~7–9 days of focused engineering.** The phase can land one slice at a time:

1. **Slice A — backend** (items 3–11): the diagnostics store + service + HTTP routes. Verifiable via ctest and curl.
2. **Slice B — MCP plugin** (item 12): exposes the new endpoints to Claude Code. Verifiable from this Claude Code session.
3. **Slice C — shell UI** (items 13–20): operator surface + Windows file picker.
4. **Slice D — browser UI** (items 21–23): browser surface for remote operators.
5. **Slice E — tests + completion report** (items 24–25).

Each slice is independently usable. Slice A alone closes the persistence + aggregation gap; Slices C/D add UI for non-Claude-Code surfaces.

## Risks

| Risk | Mitigation |
|---|---|
| SQLite write contention with the existing `mcp-state\mcos-orchestration.sqlite` operator-MCP file. | Diagnostics DB is a separate file under `diagnostics\diagnostics.db`. No shared schema. WAL mode keeps writers from blocking readers. |
| Self-test snapshot is large (multi-KB JSON); inserting into SQLite on every boot bloats the table. | `recordSelfTest` writes ONE record per snapshot with the snapshot serialized into `data_json`. A scheduled retention pass (also in this phase) prunes self-test records older than the last 50 boots. |
| WinUI 3 `FileSavePicker` requires HWND association in Win32 apps; the shell may need `IInitializeWithWindow` interop. | Use the same interop pattern already used by `ExportsSectionControl` for `OpenExportFolderButton_Click` (verify on read of that file before implementation). |
| `appendEvent` shim using `std::function` adds a per-call indirection in hot paths (e.g., per-request HTTP logging). | The shim is only invoked when `DiagnosticsService::reportFromAppendEvent` is bound; cost is one branch + one virtual call. Negligible vs the existing JSON serialization + file I/O `appendEvent` already does. |
| Adding `sqlite3` to `vcpkg.json` may require a fresh vcpkg install in CI. | Sqlite3 is already an extremely common vcpkg port; no new toolchain risk. PHASE-10's CI gate covers this. |
| Browser cannot open native file picker (per HTML spec). | Documented in the phase doc and the user-facing instruction. Browser export uses standard `Blob` download — the user gets the OS Save dialog from the browser, which is acceptable. The native file picker requirement is satisfied by the WinUI Shell. |

## Dependencies

- ADR-002 (gateway-first realignment) — locked. Unchanged.
- PHASE-09 (dashboard realignment) — provides the browser dashboard surface that this phase extends with a new tab.
- PHASE-08 (telemetry model) — `DiagnosticsService` deliberately does NOT replace telemetry. Telemetry stays in `telemetry.jsonl`; diagnostics is for events/errors/warnings. The two are sister modules.
- The boot self-test machinery already in `MasterControlRuntime.cpp` (v0.9.69) — provides the `SelfTestSnapshot` payload that PHASE-14 persists.

## Cross-references

- [`include/MasterControl/MasterControlDiagnostics.h`](../../include/MasterControl/MasterControlDiagnostics.h) — current logger; this phase extends, not replaces.
- [`src/MasterControlApp/MasterControlRuntime.cpp`](../../src/MasterControlApp/MasterControlRuntime.cpp) — host of the new HTTP routes and service construction (line ranges identified during Slice A).
- [`src/MasterControlShell/ExportsSectionControl.xaml.cpp`](../../src/MasterControlShell/ExportsSectionControl.xaml.cpp) — pattern reference for the file-picker buttons.
- [`.claude-plugin/mcos-control/mcp-servers/mcos-bridge/server.py`](../../.claude-plugin/mcos-control/mcp-servers/mcos-bridge/server.py) — pattern for adding MCP tools.

## Operator approval gate

This phase modifies MCOS source. Per CLAUDE.md "Required execution behavior" §6 and `.claude/rules/40-validation-reporting.md`, **no source-tree edits are made until the operator approves this plan**.

To approve: reply with "PHASE-14 approved" or "PHASE-14 approved, start with Slice X" (where X ∈ {A, B, C, D, E}).

To revise: reply with the specific changes (e.g., "drop the browser UI", "use WAL-only Postgres instead of SQLite", "skip the Clear button").
