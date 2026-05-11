# Release v0.10.11

- Version: `0.10.11`
- Release date: `2026-05-11`

## Summary
LAN MCP Gateway + Supervisor Wizard + tile-grid shell. Aggregate release line spanning v0.9.4 through v0.10.11 since the v0.7.0 production-milestone baseline.

## Highlights
- MCPJungle substrate retired in v0.9.0; native HTTP.sys is the only shipping gateway (`0.0.0.0:cfg.mcpGateway.listenPort` at `cfg.mcpGateway.mcpPath`, defaults `8080:/mcp`).
- Supervisor Agent Assignment Wizard: operator picks one of `chatgpt` / `claude` / `grok`. Lifecycle off → config_generated → pending_connection → connected → disconnected | revoked. Heartbeat watchdog flips Connected → Disconnected after 120s.
- WinUI Shell tile-grid renderer (`buildFooterStyleTile<StatT>`) used by Telemetry MCP / Sub-Agent panels (v0.10.6 → v0.10.7), Runtime MCP / Sub-Agent panels (v0.10.9), and the cross-tab SUB-AGENT GRID footer. 7-column grid wraps to additional rows automatically.
- Cross-tab SUB-AGENT GRID footer scoped to Telemetry + Runtime (v0.10.10). Overview MCP / Sub-Agent summary cards removed.
- Supervisor config endpoint fix (v0.10.8): `server.mcpEndpoint` now `http://<lanIp>:<gatewayPort>/mcp` (was `http://127.0.0.1:<browserPort>/mcp` — wrong on both host and port).
- Persistent Diagnostics log: supervisor lifecycle + boot self-test failures + per-boot summaries dual-emit to `<PUBLIC>\Documents\Master Control Orchestration Server\logs\<component>\events.jsonl`.
- Boot self-test count grew from ~30 to 39 probes; failures dual-emit to the Diagnostics log.
- Telemetry log throttle (v0.10.5): dashboard-snapshot writes to `telemetry.jsonl` capped at once per 60s. Cut log growth from ~21 MB/day to ~350 KB/day.
- `scripts\Deploy-LocalLive.ps1` + `DEPLOY_LOCAL_LIVE` CMake target for hot-deploy without full MSI.
- Pool orchestration scaffolding under `.claude/agents/`, `.claude/scripts/`, `.claude/mcp-state/`.

## Live state on the reference host
- 26 MCP servers (25 reachable)
- 7 sub-agents (7 reachable)
- 97 advertised gateway tools
- 39/39 boot self-tests pass

## Commit
- `dcd1e8b` (release: v0.9.3 -> v0.10.11)

See `CHANGELOG.md` for the per-area Added / Changed / Removed / Fixed breakdown.
