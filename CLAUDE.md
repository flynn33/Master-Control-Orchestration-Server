# CLAUDE.md — MCOS Realignment Instructions

## Mission
Realign Master Control Orchestration Server (MCOS) into a Windows-native LAN MCP Gateway host.

MCOS must no longer embed direct AI-provider execution. External AI clients running on client machines connect over a trusted LAN to one MCOS-advertised MCP Gateway and then use shared MCP servers, sub-agents, and CLU/Forsetti governance resources.

## Non-negotiable architecture rules

- Do not reintroduce direct provider execution inside MCOS.
- Treat ChatGPT, Codex, Claude Code, Grok, and generic agents as external clients, not embedded providers.
- LAN AI-client surfaces require no application-layer authentication. Trust is enforced at the network level.
- Keep the admin/maintainer surface logically separate from the AI-client MCP Gateway surface.
- Use a gateway pattern: clients connect to one MCOS-advertised MCP endpoint.
- Gateway substrate is the in-process Windows-native HTTP.sys adapter (`NativeHttpSysGatewayAdapter`) behind the `IMcpGateway` interface. This is the only shipping substrate. Any new substrate must implement `IMcpGateway` and be wired through `cfg.mcpGateway.type` so the topology stays single-endpoint to clients.
- MCOS remains the Windows-native C++20 orchestration host.
- Use Windows-native APIs first: Win32, HTTP.sys, WinHTTP, DNS-SD APIs, Job Objects, PDH, DXGI, ETW/TraceLogging where appropriate.
- Preserve Forsetti module boundaries and do not modify vendored Forsetti framework code.
- CLU governance distributes Forsetti guidance and evaluates governance decisions. It is not a provider-authentication system.
- All implementation must proceed in explicitly labeled phases from `handoff/realignment/manifest.json`.

## Required execution behavior

Before editing any file:

1. Read `handoff/realignment/00-START-HERE.md`.
2. Read `handoff/realignment/manifest.json`.
3. Read the active phase Markdown file.
4. Read the listed `readFirst` files for that phase.
5. Produce a file-by-file edit plan.
6. Stop for approval if the phase file says approval is required before edits.

After each phase:

1. Run the strongest available validation for touched files.
2. Update or create the required handoff note.
3. List files changed.
4. List validation commands and results.
5. List unresolved risks.
6. Do not proceed to the next phase until explicitly instructed.

## Required validation baseline

Prefer these commands where applicable:

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-mastercontrol-forsetti.ps1
```

If a command cannot run in the current environment, document the exact reason and provide the closest static validation performed instead.

## Forbidden behavior

- No broad refactors outside the active phase.
- No fake telemetry, fake health, or placeholder success states.
- No version bump until the release/hardening phase explicitly requires it.
- No hidden provider-era fallback routes.
- No MCP worker clone registration as separate public tools unless the active phase explicitly authorizes it.
- No Docker-only implementation for core Windows behavior.
- No removal of manual setup paths; manual and import paths are first-class.
- No modification of vendored Forsetti framework internals.
- No AI attribution of any kind in repository-authored output. Do not identify work as produced, assisted, reviewed, generated, co-authored, or authored by an AI system, model, bot, assistant, coding agent, vendor, or tool in commits, pull requests, issues, review comments, changelogs, release notes, documentation, source comments, generated files, or metadata under agent control unless Flynn explicitly asks for that exact attribution.
- No automated-assistance signatures, model names, assistant names, vendor names, bot names, or AI-related co-author trailers in final repository output.

## Large-project instruction structure

Use these files as the source of truth:

- `.claude/rules/` for persistent architectural constraints.
- `.claude/skills/` for reusable task procedures.
- `.claude/agents/` for specialized review agents.
- `handoff/realignment/` for phase implementation packets.
- `docs/implementation/` for durable architecture, schemas, and contracts.
