## master-control-dashboard - Project Overview

### What It Is
Real-time monitoring dashboard for the entire BLADE MCP infrastructure.
Single HTML page served by a Node.js static server via Caddy reverse proxy.

### Location
- Source: D:\mcp\dashboard\index.html (81KB, single-file SPA)
- Config: D:\mcp\dashboard\config.json (25 backend endpoints)
- Server: D:\mcp\dashboard\serve.ps1 (Node.js static server on port 18000)
- URL: http://192.168.1.3:8080/dashboard/

### Dashboard Sections
1. System Metrics - CPU, RAM, Network (real-time charts via /api/metrics/stream SSE)
2. MCP Server Grid - 18 blade servers with status indicators (green/red/yellow)
3. Sub-Agent Grid - 7 AI agents with status, uptime, tool counts
4. Agent Communication - Message flow between agents via agent-comm server
5. Coordination - Task coordination status
6. Event Bus - Event stream monitoring
7. Memory Beacon - Memory server status and beacon health

### API Endpoints Consumed
- /api/metrics + /api/metrics/history + /api/metrics/stream (SSE)
- /api/clients (client tracker)
- /api/sub-agents (WATCHTOWER aggregated data)
- /api/agent-comm (agent communication dashboard)
- /api/coordination (task coordination)
- /api/event-bus (event stream)
- /api/memory-beacon (memory beacon status)

### Architecture
- Pure HTML/CSS/JS, no build step, no dependencies
- Uses CSS Grid for responsive layout
- EventSource for real-time SSE streaming
- 5-second polling interval for non-SSE data
- Custom canvas charts (60-point rolling window)

### Recent Changes
- Fixed sub-agent grid media query: changed @media (max-width: 1400px) from repeat(auto-fit, minmax(180px, 1fr)) to repeat(7, 1fr) so all 7 agents show in one row on 1080p
- Dashboard height reduced from 958px to 847px after fix
