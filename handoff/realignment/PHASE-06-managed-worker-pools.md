---
phase: PHASE-06
label: Managed MCP/sub-agent worker pools
objective: Turn MCP servers and sub-agents into supervised managed endpoint pools.
---


# PHASE-06 — Managed MCP/Sub-Agent Worker Pools

## Goal

MCP servers and sub-agents become managed endpoint pools behind stable logical endpoints.

## Required classes/models

- `EndpointTemplate`
- `EndpointInstance`
- `ManagedEndpointPool`
- `WorkerSupervisor`
- `HealthProbe`
- `WorkerTelemetry`

## Lifecycle states

- `configured`
- `starting`
- `ready`
- `busy`
- `draining`
- `failed`
- `stopped`

## Windows process rule

Use Job Objects for worker process trees. Every supervised child must have a cleanup path that terminates or drains the process tree.

## Exit criteria

- At least one MCP pool and one sub-agent pool can be represented.
- No template appears as live infrastructure until configured and reachable.
- Lifecycle tests exist.

## Read first

- `src/MasterControlApp/MasterControlRuntime.cpp`
- `include/MasterControl`
- `tests/MasterControlOrchestrationServerTests.cpp`
- `docs/wiki/Sub-Agents.md`

## Deliverables

- EndpointTemplate
- EndpointInstance
- ManagedEndpointPool
- WorkerSupervisor
- Job Object lifecycle
- Health/readiness states

## Acceptance criteria

- MCP servers and sub-agents use same operational abstraction
- Worker process trees are supervised
- Health states visible through API
- No fake live infrastructure

## Validation

- `Supervisor unit tests`
- `Lifecycle tests`
- `Static Windows handle cleanup review`

