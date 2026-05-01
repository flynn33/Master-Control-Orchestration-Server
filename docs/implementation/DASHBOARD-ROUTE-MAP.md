# Dashboard Route Map (PHASE-09)

This file maps each browser dashboard panel to the HTTP route(s) that feed it. It is the canonical reference for: PHASE-10 release-gate review, future maintenance, and any operator who needs to know which API to hit by hand.

Snapshot: 2026-05-01 — `master-control-dashboard-main`, post-PHASE-09.

## Surface map

| Destination (nav) | Renderer | Primary feeding routes | Source phase | Notes |
|---|---|---|---|---|
| Overview | `renderOverview` | `/api/health`, `/api/dashboard`, `/api/gateway/status`, `/api/gateway/health`, `/api/pools`, `/api/pools/{id}/{leases,saturation}`, `/api/telemetry/clients`, `/api/telemetry/events?max=200`, `/api/clu/approvals` | PHASE-02..08 | Top-level KPIs: gateway state/health, pool count, ready instances, active leases, live clients, governance posture, host telemetry, recent telemetry events. |
| Gateway | `renderGatewayPanel` | `/api/gateway/status`, `/api/gateway/health`, `/api/gateway/tools` | PHASE-02 | Adapter type, lifecycle state, health probe, advertised MCP URL, registered tools list (logical-server scoped). |
| Pools | `renderPoolsPanel` / `renderPoolCard` | `/api/pools`, `/api/pools/{id}/leases`, `/api/pools/{id}/saturation` | PHASE-06 / PHASE-07 | Per-pool card: kind, scale policy, utilization meter, instance lifecycle table, lease list. Actions: scale-to-min (`POST /api/pools/{id}/scale`), drain (`POST /api/pools/{id}/drain`). |
| Clients (presence) | `renderTelemetryClients` | `/api/telemetry/clients` | PHASE-08 | Heartbeat-driven roster keyed by `clientId`. Honest defaults: `-1.0` self-reported metrics render as `unavailable` (ADR-002 §9). |
| LAN Identity (operator) | `renderClients` | `/api/clients`, `/api/clients/{id}/{config,privileges,enable,disable,autonomous-mode}` | ADR-001 | Per-client identity, privileges, autonomous mode, config bundle download. Preserved on operator surface. |
| Governance | `renderGovernance` | `/api/dashboard` (posture/rules), `/api/clu/approvals`, `/api/governance/bundles/{platform}` | PHASE-05 | CLU posture, approvals queue, governance bundles per platform (windows/macos/ios) with checksums and direct-download link. |
| Onboarding | `renderOnboardingPanel` | `/api/onboarding/{clientType}` | PHASE-04 | Profiles for `claude-code` / `codex` / `grok` / `chatgpt` / `generic-mcp`. Manual setup is always first-class. Config snippets with copy-to-clipboard. |
| Discovery | `renderDiscoveryPanel` | `/api/discovery` | PHASE-03 | Discovery document MCOS advertises: instance ID, gateway URL, governance bundle URL, onboarding paths, auth/trust/protocol versions. |
| Shared Fabric | `renderRuntime` | `/api/dashboard` (legacy `endpoints[]`) | ADR-001 | Operator catalog of registered backends. Pools (PHASE-06) are above; this remains the operator inventory view. |
| Activity | `renderActivity` | `/api/telemetry/events?max=200`, `/api/activity` | PHASE-08 + ADR-001 | Two side-by-side rings: PHASE-08 telemetry events (system/gateway/worker/client/discovery/governance × info/warning/error/critical), and the legacy admin activity ring. |
| Exports | `renderExports` | `/api/exports` | ADR-001 | Server-authored config bundles for download. Manual download path preserved. |

## Honest telemetry rule (ADR-002 §9)

The dashboard **must** render `-1.0` self-reported metrics as the literal string "unavailable", **never** as "0%" or "idle". Implementation: every numeric render that consumes a `ClientHeartbeat` or `WorkerTelemetry` field calls `formatMetric()` (in `resources/web/app.js`). Direct `value.toFixed(0) + '%'` patterns at metric sites are forbidden by FORBIDDEN-CONTRACT §8.1.

`HostTelemetrySnapshot` (PDH-derived) is exempt — those values are measured directly, so `0.0` is genuinely "idle". The overview "Host Telemetry" panel and the summary band still render host CPU/memory/disk with the legacy `0%` semantics.

## Manual setup paths preserved

PHASE-09 does not remove any manual onboarding affordance:
- Onboarding panel: every client-type profile shows manual instructions and a copyable config snippet.
- Exports panel: server-authored bundles can be downloaded as files.
- Governance panel: bundle JSON has a direct `<a download>` link.
- Discovery panel: shows the gateway / governance / onboarding URLs operators can paste into clients by hand.

## Surface compliance hard rules

These IDs MUST exist in `index.html` (Forsetti compliance, enforced by `scripts/check-mastercontrol-forsetti.ps1`):
- `id="surfaceToolbar"`
- `id="surfaceNavigation"`
- `id="surfaceContentHost"`
- `id="surfaceOverlayDialog"`

These IDs MUST NOT exist (legacy hardcoded surfaces forbidden):
- `id="telemetryGrid"`
- `id="endpointTable"`

These strings MUST NOT appear in `app.js` (provider-era residue forbidden by ADR-001 §1, ADR-002 §1):
- `dashboard-clu`, `clu-nav`, `clu-surface` (hardcoded CLU bootstrap)
- `renderSignInCards`, `/api/providers` (provider-era surfaces)
