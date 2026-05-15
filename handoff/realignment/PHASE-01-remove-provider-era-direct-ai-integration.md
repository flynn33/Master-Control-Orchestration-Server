---
phase: PHASE-01
label: Remove provider-era direct AI integration
objective: Remove or quarantine direct AI-provider execution and provider assignment paths.
---


# PHASE-01 — Remove Provider-Era Direct AI Integration

## Goal

Remove or quarantine direct AI-provider execution paths so MCOS becomes an external-client gateway host.

## Required changes

- Replace provider execution concepts with external client onboarding concepts.
- Preserve model names only as client profile targets: `chatgpt`, `codex`, `claude-code`, `grok`, `generic-mcp`.
- Remove UI/API flows that imply MCOS logs into or directly executes these providers.
- Keep manual setup/import paths first-class.
- Update tests that previously expected provider execution.

## Migration rule

If old config exists, migrate it to either:

- onboarding profile state, or
- archived legacy config with explicit warning.

Do not silently reinterpret provider credentials as gateway credentials.

## Exit criteria

- Grep confirms no direct provider execution route remains active.
- Browser and shell no longer present direct provider execution as core workflow.
- Build/tests updated.

## Read first

- `src/MasterControlApp/MasterControlRuntime.cpp`
- `src/MasterControlModules/MasterControlModules.cpp`
- `src/MasterControlShell`
- `resources/web/app.js`
- `tests/MasterControlOrchestrationServerTests.cpp`

## Deliverables

- ExternalClient model or migration path
- Provider-era direct execution removed/quarantined
- Docs updated from provider execution to external client onboarding

## Acceptance criteria

- No ChatGPT/Codex/Claude/Grok direct execution path remains in MCOS
- Client model is external-agent oriented
- Tests updated for semantic reset

## Validation

- `cmake --preset debug`
- `cmake --build --preset debug`
- `ctest --preset debug --output-on-failure`

