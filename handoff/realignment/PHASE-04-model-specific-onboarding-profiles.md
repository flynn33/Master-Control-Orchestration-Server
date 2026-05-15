---
phase: PHASE-04
label: Model-specific onboarding profiles
objective: Generate configuration profiles for Claude Code, Codex, Grok, ChatGPT connector-edge, and generic MCP clients.
---


# PHASE-04 — Model-Specific Onboarding Profiles

## Goal

Provide configuration data tailored to common AI clients while preserving generic fallback behavior.

## Required endpoints

- `/api/onboarding/generic`
- `/api/onboarding/claude-code`
- `/api/onboarding/codex`
- `/api/onboarding/grok`
- `/api/onboarding/chatgpt`

## Required behavior

- Every profile points to one MCOS gateway MCP URL.
- Every profile includes governance bundle URL.
- Every profile states `authRequired=false` for LAN gateway.
- Generic profile works for unknown MCP clients.
- ChatGPT profile must distinguish local/connector-edge constraints from LAN clients.

## Companion utility pattern

If a client cannot auto-discover or auto-apply config natively, MCOS may ship or document a small companion utility that:

1. Browses DNS-SD.
2. Fetches `/api/onboarding/{clientType}`.
3. Writes or prints the correct client config.
4. Verifies connectivity.

## Exit criteria

- Snapshot tests prove all profiles are stable.
- UI uses profiles rather than raw blank forms.

## Read first

- `resources/web/app.js`
- `docs/wiki/Client-Config-Bundle.md`
- `src/MasterControlApp/MasterControlRuntime.cpp`
- `.mcp.json`

## Deliverables

- /api/onboarding/{clientType}
- Generic fallback profile
- Config snippets
- Verification instructions
- Companion utility plan if auto-apply is not feasible

## Acceptance criteria

- Every profile uses one gateway MCP URL
- Profiles state authRequired=false for LAN gateway
- ChatGPT path is documented as connector-edge/optional where needed
- No direct provider credentials collected by MCOS for gateway use

## Validation

- `Schema tests`
- `Snapshot tests for profiles`
- `Manual docs review`

