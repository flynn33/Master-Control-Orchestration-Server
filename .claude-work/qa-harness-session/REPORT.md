# Master Control Orchestration Server — Feature Test Report

**Generated:** 2026-04-12T14:11:25.817710Z

## Summary

- PASS:  22
- WARN:  0
- FAIL:  0
- SKIP:  0

## Timeline

- `2026-04-12T14:11:25.187Z` **[OK]** GET /api/health (46ms) — HTTP 200
- `2026-04-12T14:11:25.195Z` **[OK]** GET /api/config (8ms) — HTTP 200
- `2026-04-12T14:11:25.253Z` **[OK]** GET /api/dashboard (52ms) — HTTP 200
- `2026-04-12T14:11:25.268Z` **[OK]** GET /api/activity (14ms) — HTTP 200
- `2026-04-12T14:11:25.321Z` **[OK]** telemetry fields (51ms) — host=WEBSERVER cpu=6.9% mem=11.0% disk=57.8%
- `2026-04-12T14:11:25.322Z` **[OK]** endpoint inventory — 39 endpoints
- `2026-04-12T14:11:25.344Z` **[OK]** forsetti modules (21ms) — 19 modules
- `2026-04-12T14:11:25.397Z` **[OK]** provider capabilities — 4 capabilities: chatgpt, claude-code, codex, xai-grok
- `2026-04-12T14:11:25.428Z` **[OK]** auto-connect codex (30ms) — id=chatgpt-20260412-091125 latency=27ms
- `2026-04-12T14:11:25.464Z` **[OK]** auto-connect claude_code (35ms) — id=claude-code-20260412-091125 latency=32ms
- `2026-04-12T14:11:25.503Z` **[OK]** auto-connect codex (38ms) — id=chatgpt-20260412-091125 latency=25ms
- `2026-04-12T14:11:25.540Z` **[OK]** auto-connect xai (35ms) — id=xai-grok-20260412-091125 latency=33ms
- `2026-04-12T14:11:25.560Z` **[OK]** providers persisted — 6 providers in config
- `2026-04-12T14:11:25.617Z` **[OK]** assignment targets — 11 targets available
- `2026-04-12T14:11:25.628Z` **[OK]** upsert mcp server (9ms) — 
- `2026-04-12T14:11:25.647Z` **[OK]** remove mcp server (18ms) — 
- `2026-04-12T14:11:25.667Z` **[OK]** upsert sub-agent (20ms) — 
- `2026-04-12T14:11:25.686Z` **[OK]** upsert sub-agent group (17ms) — 
- `2026-04-12T14:11:25.781Z` **[OK]** clu profile (54ms) — 4 roles, 7 rules, 1 findings
- `2026-04-12T14:11:25.797Z` **[OK]** exports list (14ms) — 7 artifacts
- `2026-04-12T14:11:25.816Z` **[OK]** activity fetch (18ms) — hwm=95 events=95
- `2026-04-12T14:11:25.816Z` **[OK]** activity event shape — all events well-formed
