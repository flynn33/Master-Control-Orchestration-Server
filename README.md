# master-control-dashboard

Master Control Program is a Forsetti-compliant Windows control plane for MCP servers, sub-agents, host telemetry, and browser-based operations.

## Current Release

- Version: `v0.1.13`
- Release date: `2026-03-21`
- Summary: Automated patch release for Master Control Program.

## Highlights

- Windows Service host for configuration, telemetry, imports, provider integration, and LAN beaconing.
- Authored WinUI 3 desktop shell with Tron-inspired visuals and a browser dashboard backed by the same service APIs.
- Import and install paths for MSI, EXE, PS1, Git bootstrap repositories, and manifest-driven zip bundles.
- Forsetti-aligned modules for environment discovery, runtime inventory, configuration, export generation, and gateway control.

## Repository Layout

- `src/` - application hosts and shared runtime implementation.
- `include/` - shared contracts, models, and defaults.
- `resources/` - web assets and Forsetti manifests.
- `plans/` - current architecture and infrastructure notes.
- `docs/wiki/` - wiki source pages maintained by repository automation.
- `docs/versions/` - generated release documentation.

## Build And Validate

```powershell
cmake --build build\debug --config Debug
ctest --test-dir build\debug -C Debug --output-on-failure
cmake --install build\debug --config Debug --prefix dist\debug
```

## GitHub Agents

| Agent | Responsibility | Trigger |
| --- | --- | --- |
| Changelog Agent | Updates `CHANGELOG.md` after pushes to `main`. | `push`, `workflow_dispatch` |
| Wiki + README Agent | Regenerates `README.md`, wiki source pages, and syncs the GitHub wiki. | `push`, `workflow_dispatch` |
| AI Contributor Guard | Rejects commits that declare AI contributors and can block pushes locally. | `pre-push`, `push`, `pull_request` |
| Version Agent | Tracks semantic versions and creates the next numbered release. | `push`, `workflow_dispatch` |
| Version Documentation Agent | Generates release pages in `docs/versions/` and publishes GitHub releases. | `push`, `workflow_dispatch` |

## Key Project Notes

### Dashboard Overview
- Real-time monitoring dashboard for the entire BLADE MCP infrastructure.
- Single HTML page served by a Node.js static server via Caddy reverse proxy.
- Source: D:\mcp\dashboard\index.html (81KB, single-file SPA)
- Config: D:\mcp\dashboard\config.json (25 backend endpoints)
- Server: D:\mcp\dashboard\serve.ps1 (Node.js static server on port 18000)
- URL: http://192.168.1.3:8080/dashboard/
- System Metrics - CPU, RAM, Network (real-time charts via /api/metrics/stream SSE)
- MCP Server Grid - 18 blade servers with status indicators (green/red/yellow)

### Technical Snapshot
- Caddy route: /dashboard* -> reverse_proxy 127.0.0.1:18000 (uri strip_prefix)
- Static server: Node.js on port 18000 (managed by serve.ps1)
- All API calls proxied through Caddy :8080
- repo-search(7101), docs-search(7102), fs-cache(7103), build-cache(7104), symbol-index(7105), session-context(7106), response-cache(7107), git-intel(7108), file-digest(7109), vector-search(7110), dep-graph(7111), lint-cache(7112), snippet-store(7113), task-queue(7114), memory(7115), agent-comm(7116), coordination(7117), event-bus(7118)
- sentinel(7201), architect(7202), forge(7203), scribe(7204), recon(7205), nexus(7206), watchtower(7207)
- aggregator-gateway(7200), client-tracker(7120), metrics(7121)
- Grid-based: .server-grid uses repeat(auto-fit, minmax(180px, 1fr))
- Agent grid: .agent-grid uses repeat(7, 1fr) (forced 7 columns)

## Primary Documents

- [Project Overview](plans/dashboard/project-overview.md)
- [Technical Details](plans/dashboard/technical-details.md)
- [Aggregator Gateway Plan](plans/infrastructure/mcp-aggregator-gateway-deployment.md)
- [Version Index](docs/versions/index.md)
- [Latest Release Notes](docs/versions/latest.md)

Repository URL: https://github.com/flynn33/master-control-dashboard
