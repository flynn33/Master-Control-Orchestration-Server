# Master Control Orchestration Server Sub-Agents

This page reflects the current seven-agent architecture and shared platform gateway client model.

### Architecture
7 AI sub-agents running on ports 7201-7207:
- SENTINEL (7201): Design validation, import checking, dependency validation, guardrails
- ARCHITECT (7202): Project analysis, design review, dependency checks, pattern suggestions
- FORGE (7203): Build execution, test running, pipeline management, file watching
- SCRIBE (7204): API documentation, code explanation, knowledge search, docs updates
- RECON (7205): Diff review, file analysis, pattern finding, security scans, quality reports
- NEXUS (7206): Workflow orchestration, task management, agent roster, request aggregation
- WATCHTOWER (7207): Health monitoring, agent metrics, alert history, restart capability

### Critical Implementation Details
- SSE Response Parsing required: MCP SDK returns text/event-stream, not JSON
- Must send notifications/initialized after MCP init handshake
- Accept header must include both application/json and text/event-stream
- All agents share the platform gateway client for MCP communication

### Key Files
- D:\Sub-Agents\lib\platform-gateway-client.js - SSE-aware MCP client
- D:\Sub-Agents\lib\agent-base.js - Shared server factory
- D:\Sub-Agents\agents\{name}\index.js - Individual agent implementations
- D:\Sub-Agents\Start-SubAgents.ps1 / Stop-SubAgents.ps1 - Lifecycle management

### Resource Usage
- ~540MB total RAM for all 7 agents (~77MB each)
- All registered with the orchestration agent-communication server
- WATCHTOWER aggregates all agent data via /api/sub-agents endpoint

See also: [Architecture](Architecture) | [API Reference](API-Reference)
