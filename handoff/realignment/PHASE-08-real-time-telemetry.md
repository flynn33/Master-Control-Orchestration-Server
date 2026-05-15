---
phase: PHASE-08
label: Real-time telemetry model
objective: Collect and expose host/client/gateway/worker telemetry.
---


# PHASE-08 — Real-Time Telemetry Model

## Goal

Expose real-time host, client, gateway, worker, and activity telemetry.

## Required host metrics

- CPU utilization
- GPU utilization or honest unavailable state
- GPU memory usage where available
- Network throughput
- Disk utilization / I/O
- Memory where already available

## Required client metrics

- Connected client/model roster
- IP address
- Gateway session/request metrics
- Client-reported CPU/GPU/network/disk only if supplied by heartbeat/sidecar

## Required worker metrics

- Pool ID
- Instance ID
- Health
- Active leases
- Queue depth
- Inflight calls
- CPU/memory/I/O where available

## Exit criteria

- No fake utilization.
- Missing GPU/client metrics degrade gracefully.
- Activity log includes connections, warnings, errors, scale events, governance events.

## Read first

- `src/MasterControlApp/MasterControlRuntime.cpp`
- `resources/web/app.js`
- `docs/wiki/Telemetry-and-Activity.md`

## Deliverables

- Host CPU/GPU/network/disk telemetry
- Client heartbeat schema
- Worker telemetry
- Gateway telemetry
- Activity event taxonomy

## Acceptance criteria

- Dashboard APIs expose real metrics or unavailable states
- No fake client CPU/GPU without heartbeat
- Warnings/errors stream in real time
- Worker pool telemetry includes utilization and waits

## Validation

- `Telemetry schema tests`
- `Activity serialization tests`
- `Static metric-source review`

