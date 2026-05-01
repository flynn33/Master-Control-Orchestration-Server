# MCOS Realignment — Start Here

## Read order for Claude Code

1. `CLAUDE.md`
2. `handoff/realignment/manifest.json`
3. This file
4. The active phase file
5. The phase `readFirst` files listed in `manifest.json`
6. Relevant `.claude/rules/` files
7. Relevant `.claude/skills/` file

## Implementation rule

Implement exactly one phase at a time.

Use this command pattern with Claude Code:

```text
Implement PHASE-00 only. Read CLAUDE.md, handoff/realignment/manifest.json, and handoff/realignment/PHASE-00-repo-baseline-and-adr-lock.md before editing. Produce a file-by-file plan first.
```

Then continue:

```text
Implement PHASE-01 only ...
```

## Phase labels

- PHASE-00 — Repository baseline and ADR lock
- PHASE-01 — Remove provider-era direct AI integration
- PHASE-02 — MCP Gateway spike with MCPJungle adapter
- PHASE-03 — Bonjour-style LAN discovery and beacon correction
- PHASE-04 — Model-specific onboarding profiles
- PHASE-05 — CLU/Forsetti governance bundle distribution
- PHASE-06 — Managed MCP/sub-agent worker pools
- PHASE-07 — Autoscaling and lease routing
- PHASE-08 — Real-time telemetry model
- PHASE-09 — Tron dashboard realignment
- PHASE-10 — Windows hardening, CI, packaging, and release gate
- PHASE-11 — Native gateway evaluation and MCPJungle replacement option

## Completion rule

Every phase must produce a phase completion report using `handoff/realignment/templates/phase-completion-report-template.md`.

