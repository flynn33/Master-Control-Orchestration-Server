---
phase: PHASE-00
label: Repository baseline and ADR lock
objective: Freeze the new architecture in repo docs before code changes.
---


# PHASE-00 — Repository Baseline and ADR Lock

## Goal

Before code changes, freeze the new MCOS direction in durable repo docs.

## Required changes

- Add an ADR replacing or superseding ADR-001 where it conflicts with the new gateway-first design.
- Document that AI clients are external and MCOS does not execute AI providers directly.
- Document MCP Gateway, LAN Discovery, CLU Governance, Worker Supervisor, Lease Router, and Telemetry Aggregator as first-class subsystems.
- Create a source inventory of provider-era routes, client identity assumptions, beacon/discovery code, CLU endpoints, MCP/sub-agent catalog code, telemetry code, shell/browser surfaces, and tests.

## Do not

- Do not change runtime behavior in this phase unless needed to keep docs indexed.
- Do not remove old code yet.
- Do not rename public contracts yet.

## Exit criteria

- New ADR exists and is linked from docs.
- Provider-era removal map exists.
- Forbidden contract grep list exists.
- Next phase has exact files to edit.

## Read first

- `README.md`
- `CLAUDE.md`
- `AGENTS.md`
- `docs/wiki/Architecture-Decisions/ADR-001-lan-client-control-plane.md`
- `src/MasterControlApp/MasterControlRuntime.cpp`
- `src/MasterControlModules/MasterControlModules.cpp`

## Deliverables

- New ADR for gateway-first realignment
- Architecture drift inventory
- Provider-era removal map
- Baseline forbidden-contract grep list

## Acceptance criteria

- New ADR states MCOS is not direct AI executor
- MCP Gateway and LAN discovery are first-class modules
- Current provider/client/auth semantics documented before change

## Validation

- `Static source inventory`
- `Grep for provider-era terms`
- `No code edits unless needed for docs index`

