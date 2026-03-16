# Master Control Program Architecture

## Dashboard Program
- Real-time monitoring dashboard for the entire BLADE MCP infrastructure.
- Single HTML page served by a Node.js static server via Caddy reverse proxy.
- Source: D:\mcp\dashboard\index.html (81KB, single-file SPA)
- Config: D:\mcp\dashboard\config.json (25 backend endpoints)
- Server: D:\mcp\dashboard\serve.ps1 (Node.js static server on port 18000)
- URL: http://192.168.1.3:8080/dashboard/
- System Metrics - CPU, RAM, Network (real-time charts via /api/metrics/stream SSE)
- MCP Server Grid - 18 blade servers with status indicators (green/red/yellow)
- Sub-Agent Grid - 7 AI agents with status, uptime, tool counts
- Agent Communication - Message flow between agents via agent-comm server

## Aggregator Gateway
- Single MCP server (port 7200) that proxies ALL tools from all blade servers and sub-agents.
- Remote Claude Code instances need only ONE connection to access 96+ tools.
- HTTPS endpoint: https://192.168.1.3:8443/mcp/gateway
- HTTP endpoint: http://192.168.1.3:8080/mcp/gateway
- Health check: http://192.168.1.3:7200/health
- Dashboard: http://192.168.1.3:7200/dashboard
- Low-level MCP Server class (passes JSON Schema verbatim)
- Connects to 25 backends on startup, runs tools/list, builds unified registry
- Routes tool calls by name to correct backend
- Persistent sessions with auto-reinit on expiry

## Repository Modules

- `src/MasterControlServiceHost` boots the service runtime.
- `src/MasterControlShell` provides the WinUI 3 operator shell.
- `src/MasterControlBootstrapper` handles install and detection flows.
- `resources/manifests` holds Forsetti module manifests.
