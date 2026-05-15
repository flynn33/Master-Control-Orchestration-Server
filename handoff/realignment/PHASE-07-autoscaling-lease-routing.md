---
phase: PHASE-07
label: Autoscaling and lease routing
objective: Scale endpoint pools and route new sessions/leases under load.
---


# PHASE-07 — Autoscaling and Lease Routing

## Goal

When a managed MCP server or sub-agent is heavily utilized, MCOS starts another instance of the same pool type and routes new leases to it.

## Required changes

- Add `EndpointLease`.
- Add `LeaseRouter`.
- Add `ScalePolicy`.
- Add `DrainPolicy`.
- Add queue/wait/saturation metrics.

## Session rule

Stateful MCP sessions must remain sticky while active. Move new sessions/leases to new instances. Do not hot-splice active stateful streams unless a specific backend supports it.

## Exit criteria

- Synthetic load triggers scale-out.
- New leases route to new instance.
- Draining preserves existing active sessions.
- Dashboard/API exposes saturation and active leases.

## Read first

- `docs/implementation/schemas/managed-endpoint-pool.schema.json`
- `src/MasterControlApp/MasterControlRuntime.cpp`
- `tests`

## Deliverables

- EndpointLease
- LeaseRouter
- ScalePolicy
- DrainPolicy
- Queue pressure metrics
- Synthetic load tests

## Acceptance criteria

- Heavy utilization triggers same-type scale-out
- Existing stateful sessions drain/stick
- New sessions route to scaled instance
- Dashboard/API show pool saturation

## Validation

- `Lease routing tests`
- `Scale policy tests`
- `Failure/restart tests`

