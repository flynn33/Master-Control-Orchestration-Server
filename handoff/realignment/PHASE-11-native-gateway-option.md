---
phase: PHASE-11
label: Native gateway evaluation and native HTTP.sys gateway replacement option
objective: Evaluate whether to keep native HTTP.sys gateway or implement native MCOS gateway after spike data.
---


# PHASE-11 — Native Gateway Evaluation and native HTTP.sys gateway Replacement Option

## Goal

After native HTTP.sys gateway spike and telemetry, decide whether MCOS should keep native HTTP.sys gateway or build a native gateway implementation.

## Required analysis

- native HTTP.sys gateway operational fit on Windows.
- Direct-host execution viability.
- Dependency/package burden.
- Feature gaps: session affinity, tool groups, auth/no-auth, observability, startup behavior.
- Native gateway effort estimate using HTTP.sys/WinHTTP.
- Migration plan preserving `IMcpGateway`.

## Exit criteria

- Keep/replace decision is documented.
- If native gateway is selected, new phases are proposed; do not implement native gateway in this phase.

## Read first

- `src`
- `docs/implementation`
- `handoff/realignment/PHASE-02-mcp-gateway-spike-native HTTP.sys gateway.md`

## Deliverables

- Keep/replace decision matrix
- Native gateway requirements
- Migration plan if replacing
- Operational limitations report

## Acceptance criteria

- Decision is evidence-based
- native HTTP.sys gateway limitations documented
- Native implementation scope does not break previous phases

## Validation

- `Architecture review`
- `Performance/operational notes`
- `Risk report`

