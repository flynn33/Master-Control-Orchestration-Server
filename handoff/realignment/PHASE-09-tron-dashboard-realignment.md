---
phase: PHASE-09
label: Tron dashboard realignment
objective: Rebuild dashboard around gateway, clients, workers, telemetry, and activity.
---


# PHASE-09 — Tron Dashboard Realignment

## Goal

Dashboard displays the new architecture in real time.

## Required panels

- Host telemetry: CPU/GPU/network/disk.
- Gateway status and MCPJungle/native gateway health.
- Connected clients and models with IPs.
- MCP/sub-agent pools with utilization indicators.
- Lease/router/autoscale state.
- CLU/Forsetti governance state.
- Real-time activity log.
- Onboarding/config download panel.

## UX rule

Do not expose raw internals first. Use task cards and progressive disclosure.

## Exit criteria

- Provider execution dashboard is gone.
- Gateway/pool/client telemetry is primary.
- Manual and import setup remain discoverable.

## Read first

- `resources/web/index.html`
- `resources/web/app.js`
- `resources/web/styles.css`
- `src/MasterControlShell`

## Deliverables

- Gateway status panel
- Client/model roster
- Host telemetry meters
- Worker pool gauges
- Activity log
- Governance panel
- Setup/onboarding view

## Acceptance criteria

- Dashboard no longer centers provider execution
- All required real-time telemetry appears
- Generic unavailable states are honest
- Manual/import setup paths remain visible

## Validation

- `Browser static tests if present`
- `Snapshot review`
- `Manual UI route checklist`

