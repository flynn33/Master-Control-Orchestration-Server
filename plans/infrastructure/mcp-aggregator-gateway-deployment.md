## MCP Aggregator Gateway - Deployment Plan & Status

### What It Is
Single MCP server (port 7200) that proxies ALL tools from all blade servers and sub-agents.
Remote Claude Code instances need only ONE connection to access 96+ tools.

### Connection Details
- HTTPS endpoint: https://192.168.1.3:8443/mcp/gateway
- HTTP endpoint: http://192.168.1.3:8080/mcp/gateway
- Health check: http://192.168.1.3:7200/health
- Dashboard: http://192.168.1.3:7200/dashboard

### Architecture
- Low-level MCP Server class (passes JSON Schema verbatim)
- Connects to 25 backends on startup, runs tools/list, builds unified registry
- Routes tool calls by name to correct backend
- Persistent sessions with auto-reinit on expiry
- 5-minute refresh cycle picks up new/recovered backends

### Current Stats (as of 2026-03-14)
- 96 tools from 21/25 online backends
- 4 offline: response-cache, coordination, event-bus, agent-comm (no MCP tools)
- ~95MB RAM

### Key Files
- D:\Sub-Agents\aggregator\index.js - Main server
- D:\Sub-Agents\aggregator\discovery.js - Backend discovery + SSE parser
- D:\Sub-Agents\aggregator\proxy.js - Tool call routing with retry
- D:\mcp\config\Caddyfile - Caddy routes for /mcp/gateway*

### Remote Client Setup
Run Install-BladeGatewayPlugin.ps1 on any Windows machine to:
1. Add MCP server entry to ~/.claude.json
2. Install self-signed cert (OS store + NODE_EXTRA_CA_CERTS)
3. Verify connectivity
Then restart Claude Code Desktop.
