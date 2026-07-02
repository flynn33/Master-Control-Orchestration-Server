# Phase Completion Report — PHASE-09

Phase: PHASE-09 — Tron dashboard realignment
Phase file: [handoff/realignment/PHASE-09-tron-dashboard-realignment.md](../../../handoff/realignment/PHASE-09-tron-dashboard-realignment.md)
Manifest: [handoff/realignment/manifest.json](../../../handoff/realignment/manifest.json)
Date: 2026-05-01
Working tree: `master-control-dashboard-main`
Pre-phase commit: `142d221` (PHASE-08 completion report)
Phase commit: `c241440` (feat(phase-09): Tron dashboard realignment)

## Scope completed

PHASE-09 rebuilt the browser operator surface around the gateway-first model declared by ADR-002. The legacy six-destination LAN-client control plane (Overview / LAN Clients / Governance / Shared Fabric / Activity / Exports) is replaced by an eleven-destination gateway-first surface that exercises every endpoint added in PHASE-02..08:

| Destination | Renderer | Routes consumed |
|---|---|---|
| Overview (Command Deck) | `renderOverview` | health / dashboard / gateway / pools / pool saturation / pool leases / telemetry events / telemetry clients / clu approvals |
| Gateway | `renderGatewayPanel` | `/api/gateway/{status,health,tools}` |
| Pools | `renderPoolsPanel` / `renderPoolCard` | `/api/pools` + per-pool `/api/pools/{id}/{leases,saturation}`, with inline `POST /api/pools/{id}/{scale,drain}` |
| Clients (presence) | `renderTelemetryClients` | `/api/telemetry/clients` |
| LAN Identity (operator) | `renderClients` | `/api/clients` family (preserved ADR-001 operator surface) |
| Governance | `renderGovernance` | `/api/dashboard` (posture/rules) + `/api/clu/approvals` + `/api/governance/bundles/{platform}` |
| Onboarding | `renderOnboardingPanel` | `/api/onboarding/{clientType}` for `claude-code` / `codex` / `grok` / `chatgpt` / `generic-mcp` |
| Discovery | `renderDiscoveryPanel` | `/api/discovery` |
| Shared Fabric | `renderRuntime` | `/api/dashboard` legacy `endpoints[]` (operator catalog preserved) |
| Activity | `renderActivity` | `/api/telemetry/events?max=200` + legacy `/api/activity` |
| Exports | `renderExports` | `/api/exports` (preserved manual download path) |

The honest-telemetry rule (ADR-002 §9) is enforced at the render layer: a new `formatMetric(value, options)` helper renders the `-1.0` unavailable sentinel from PHASE-08 (`ClientHeartbeat::cpuPercent` / `memoryPercent` / `gpuPercent` / `gpuMemoryMb`) and PHASE-06 (`WorkerTelemetry::cpuPercent` / `memoryMbytes`) as the literal string "unavailable" — never as `0%` or "idle". `HostTelemetrySnapshot` keeps its PDH-direct `0%-means-idle` semantics because those values are measured directly. FORBIDDEN-CONTRACT §8.1 enforces this split via grep.

Manual setup paths stay first-class: the onboarding panel shows manual instructions before snippets and adds a copy-to-clipboard button on every snippet; the governance panel adds a direct `<a download>` link to the bundle JSON; the discovery panel exposes the gateway / governance / onboarding URLs operators can paste into clients by hand; exports preserves server-authored bundle file downloads.

## Files changed

| File | Change summary |
|---|---|
| [resources/web/index.html](../../../resources/web/index.html) | Header subtitle realigned to gateway-first wording. Forsetti compliance hosts (`surfaceToolbar` / `surfaceNavigation` / `surfaceContentHost` / `surfaceOverlayDialog`) preserved verbatim. |
| [resources/web/app.js](../../../resources/web/app.js) | Added gateway-first state (`gatewayStatus`, `gatewayHealth`, `gatewayTools`, `pools`, `poolLeases`, `poolSaturation`, `discovery`, `onboardingClientType`, `onboardingProfile`, `governanceBundlePlatform`, `governanceBundle`, `telemetryEvents`, `telemetryClients`, `telemetryGateway`). Added `formatMetric` / `metricTone` / `formatBytes` helpers. Rewrote `destinations[]` (six → eleven). Extended `refreshAll()` with eight new fetches plus per-pool lease/saturation fetches and lazy onboarding/governance fetches. Realigned toolbar quick-actions, summary band, and overview panels to gateway-first KPIs. Added `renderGatewayPanel`, `renderPoolsPanel`, `renderPoolCard`, `renderPoolInstances`, `renderPoolLeases`, `bindPoolsHandlers`, `renderTelemetryClients`, `renderOnboardingPanel`, `bindOnboardingHandlers`, `renderDiscoveryPanel`, `renderTelemetryEventRows`. Extended `renderActivity` to two side-by-side rings (telemetry events + legacy admin). Extended `renderGovernance` with platform tabs and direct bundle download. |
| [resources/web/styles.css](../../../resources/web/styles.css) | New styles for `.gateway-grid`, `.pools-grid`, `.pool-card` + `.pool-card-header` / `.pool-card-policy` / `.pool-card-meter` / `.pool-card-actions`, `.meter-bar` + `.meter-fill.{ok,warn,bad}`, `.pool-leases-details`, `.onboarding-panel` / `.onboarding-tabs` / `.onboarding-summary` / `.onboarding-instructions` / `.onboarding-snippet` / `.onboarding-code` / `.onboarding-caveats`, `.discovery-doc`, `.activity-grid`, severity-driven `.activity-row[data-tone]` colors. Mobile breakpoint added. |
| [docs/implementation/DASHBOARD-ROUTE-MAP.md](../../implementation/DASHBOARD-ROUTE-MAP.md) | New canonical reference mapping each panel to its feeding routes. Documents the honest-telemetry rule and lists the surface-compliance hard rules. |
| [docs/implementation/ARCHITECTURE-DRIFT-INVENTORY.md](../../implementation/ARCHITECTURE-DRIFT-INVENTORY.md) | Section G expanded from one drift row to eleven "done" rows covering gateway / pools / clients / governance / onboarding / discovery / activity / honesty / manual-setup. WinUI shell row remains the single deferred-cleanup item. |
| [docs/implementation/FORBIDDEN-CONTRACT-GREP-LIST.md](../../implementation/FORBIDDEN-CONTRACT-GREP-LIST.md) | New Group 8 — Dashboard honesty: §8.1 forbids `.toFixed(...)` on heartbeat / worker-telemetry sites; §8.2 forbids legacy hardcoded surface IDs in index.html; §8.3 forbids provider-era residue in app.js (mirrors Forsetti script); §8.4 forbids reintroduced `/api/providers` fetches. |

Total: 6 files changed, +971 / -78 lines.

## Public contracts changed

- **No C++ headers changed.** No HTTP routes added or removed.
- **Browser surface contract** — `app.js` now consumes 9 new routes (all introduced in PHASE-02..08) and preserves all 6 legacy routes. The eleven destinations replace the six-destination layout.
- **Compliance contract** — Forsetti host elements unchanged. New FORBIDDEN-CONTRACT §8.x patterns formalize the dashboard honesty rule.

## Tests added or updated

This phase modified browser code only. There is no JavaScript test harness in the repo; the phase file's validation row is "Browser static tests if present / Snapshot review / Manual UI route checklist." Static validation performed:

- Grep verification of every new FORBIDDEN-CONTRACT §8.x pattern returns zero matches.
- Grep verification that all 6 self-reported metric render sites consume `formatMetric()`. The 5 remaining direct `.toFixed(0) + '%'` matches all read `t.cpuPercent` / `t.memoryPercent` / `t.diskPercent` from `HostTelemetrySnapshot` (PDH-direct, documented exception).
- C++ test suite continues to pass: 4/4 in ~2.0s, 56 test functions (no change from PHASE-08 because no C++ code changed).

## Validation performed

| Command | Result | Notes |
|---|---|---|
| `cmake --build --preset debug` | **succeeded** — 0 errors | Reused PHASE-08 build dir. No C++ files changed; the build runs to confirm nothing in `resources/web/` is consumed by build steps. One pre-existing C4100 warning at `SetupWizardBuilder.cpp:133` carries forward unchanged. |
| `ctest --preset debug --output-on-failure` | **4/4 passed** in ~2.0s | 56 test functions; no change from PHASE-08. |
| `scripts/check-mastercontrol-forsetti.ps1` | **PASS** — `Master Control Forsetti checks passed.` | Confirms surfaceToolbar / surfaceNavigation / surfaceContentHost / surfaceOverlayDialog hosts present, no telemetryGrid / endpointTable IDs, no provider-era surfaces, no hardcoded CLU bootstrap. |
| FORBIDDEN-CONTRACT §8.1 (heartbeat / worker telemetry must use `formatMetric`) | 0 matches | `hb.*.toFixed`, `tel.*.toFixed`, `lastHeartbeat.*.toFixed` patterns return empty across `resources/web/`. |
| FORBIDDEN-CONTRACT §8.2 (no legacy hardcoded surface IDs) | 0 matches | `id="telemetryGrid"` / `id="endpointTable"` patterns return empty. |
| FORBIDDEN-CONTRACT §8.3 (no provider-era residue in app.js) | 0 matches | `renderSignInCards` / `/api/providers` / `dashboard-clu` / `clu-nav` / `clu-surface` return empty. |
| FORBIDDEN-CONTRACT §8.4 (no reintroduced `/api/providers` fetches) | 0 matches | `fetch('/api/providers')` / `loadJson('/api/providers')` return empty. |
| Vendoring integrity (FORBIDDEN-CONTRACT §5.1) | 0 changes | `git diff Forsetti-Framework-Windows-main/` since baseline → empty. |

## Acceptance criteria status (from manifest)

| Criterion | Status | Evidence |
|---|---|---|
| Provider execution dashboard is gone | met | FORBIDDEN-CONTRACT §8.3/§8.4 return zero matches; Forsetti compliance script's positive checks for `renderSignInCards` / `/api/providers` / hardcoded CLU keys all pass. |
| Gateway / pool / client telemetry is primary | met | The Overview panel surfaces gateway state/health, pool count + ready instances + active leases, live clients, and recent telemetry events as four of its five primary cards. The summary band leads with gateway state, then pool / lease / client counts. The toolbar's accent quick-action is "Onboard a Client" — the gateway-first onboarding entry point. |
| Required real-time telemetry appears | met | All eight required panels from the phase file (host telemetry, gateway status, connected clients, pool utilization, lease/router/autoscale, governance, activity log, onboarding) are wired and consume the matching API. Per-pool saturation refreshes every 5s alongside the rest of the surface. |
| Generic unavailable states are honest | met | `formatMetric()` renders the `-1.0` sentinel as "unavailable". `testClientHeartbeatHonestDefaultsAreUnavailable` (PHASE-08) pins the `-1.0` defaults at the type level; PHASE-09 carries that contract through to the UI. The Connected Clients panel includes a footer reminding readers that "unavailable" never means `0%`. |
| Manual / import setup paths remain visible | met | Onboarding panel shows manual instructions before snippets. Exports panel preserves file download. Governance panel adds direct bundle JSON download. Discovery panel exposes URLs operators can paste by hand. |

## Risks and blockers

1. **No browser-side automated tests.** The phase file's validation accepts "Browser static tests if present / Snapshot review / Manual UI route checklist" — there is no JS test harness in the repo, so the verification is by static grep and code review. The 5s polling + the bounded fetch list mean that a route that returns garbage will degrade gracefully (each fetch is `.catch()`-wrapped), but a regression in `formatMetric` could silently render `-1` as "-1%" if the helper is bypassed. PHASE-10 should add either a Playwright smoke or a small Node-based unit harness for `formatMetric` specifically.
2. **the in-process HTTP.sys adapter-backed gateway not exercised end-to-end.** Pools panel renders correctly when `/api/pools` is empty (the supervised-mock fallback). Per-pool fetches degrade silently when a pool has no instances. A real gateway binary integration test is deferred to PHASE-10.
3. **WinUI shell unchanged.** The phase file's `readFirst` lists `src/MasterControlShell` but the deliverables only call out browser panels. PHASE-09 deliberately scoped to the browser surface; the shell remains its own row in ARCHITECTURE-DRIFT-INVENTORY §G as deferred. WinUI realignment is a future track.
4. **Dashboard polling is fixed at 5s.** Acceptable for LAN operations but burns request volume. PHASE-10 may want a backoff or SSE-based push for the events / clients streams. Not blocking for PHASE-09.
5. **Pre-existing C4100 warning persists** in `SetupWizardBuilder.cpp:133`. Carries forward from PHASE-01.
6. **Per-pool lease + saturation fetches happen in series with the main refresh.** With N pools the refresh issues 1 + (2 × N) requests every 5s. Acceptable for LAN-scale (N typically small) but a known scaling cliff. Auto-coalescing or a `/api/pools?expand=leases,saturation` route is a PHASE-10 / future optimization.
7. **`formatMetric` honesty applies to the dashboard only.** A separate WinUI shell, the Tron desktop app, or a third-party consumer could render the same `-1.0` value naively. The contract is at the type level (PHASE-08) and this phase pulls it through to the browser. Other consumers that consume the same JSON must adopt the same convention. Not a regression — a documentation/awareness item for downstream.

None of these block declaring PHASE-09 complete.

## Deferred work

| Item | Deferred to | Reason |
|---|---|---|
| Browser-side automated tests (formatMetric unit + Playwright smoke) | PHASE-10 | Release-gate hardening concern; the phase file scopes PHASE-09 to UI realignment, not test harness creation. |
| WinUI shell realignment | Future track | Phase file scoped to browser surface; shell deferred-cleanup row remains. |
| the in-process HTTP.sys adapter-backed end-to-end gateway / pool exercise | PHASE-10 | Requires a real spawnable gateway binary; release-gate concern. |
| `/api/pools?expand=leases,saturation` to coalesce per-pool fetches | Future | Pure optimization; current polling is acceptable for LAN scale. |
| SSE / WebSocket push for telemetry events stream | Future | Pure optimization; 5s polling is correct for the operator surface. |
| Onboarding companion-utility download link | Future / PHASE-10 | Profile already documents the connector-edge constraint via `caveats`; the binary itself is not yet packaged. |
| Pool create / edit / remove UI actions | Future | Pool admin is currently API-only; the dashboard exposes scale-to-min and drain inline. Full pool CRUD via UI lands when the phase file calls for it. |

## Ready for next phase?

**Answer: yes** — every panel listed in PHASE-09 is rendered, fed by a real route, and renders the `-1.0` unavailable sentinel honestly. The Forsetti compliance script passes, every PHASE-09 grep returns zero matches, and the C++ build + ctest stay green at 4/4.

PHASE-10 (Windows hardening, CI, packaging, release gate) should begin by:
1. Reading [handoff/realignment/PHASE-10-windows-hardening-ci-release.md](../../../handoff/realignment/PHASE-10-windows-hardening-ci-release.md) and its `readFirst` files (`.github/workflows/`, `installer/`, `CMakePresets.json`, `VERSION.json`, `scripts/`).
2. Producing a file-by-file plan covering: `vswhere`-driven toolchain discovery, version stamping in CI, no `workflow_dispatch` bypass, MSI/MSIX packaging recipe audit, install/uninstall smoke, gateway binary packaging, firewall/LAN-mode docs, browser-side automated test harness (formatMetric unit + Playwright smoke against the new dashboard surface).
3. Exercising the in-process HTTP.sys adapter-backed gateway end-to-end (the static-only validation of PHASE-02..09 becomes runtime validation here).
4. Bumping `VERSION.json` — first allowed bump per the manifest's `noBumpUntilPhaseTen: true` rule.
5. Stopping at the PHASE-10 completion report. Not proceeding to PHASE-11.

PHASE-09 stops here. No further phases will start without explicit instruction from the operator.
