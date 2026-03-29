# Master Control Program Sub-Agents

Seven AI sub-agents run on ports 7201 through 7207, each with a specialized
role in the development and operations lifecycle. All agents share a common
MCP communication layer and are aggregated into the single gateway endpoint.

## Agent Roster

| Agent | Port | Role | Key Capabilities |
| --- | --- | --- | --- |
| **SENTINEL** | 7201 | Validation & Guardrails | Design validation, import checking, dependency validation, enforcement rules |
| **ARCHITECT** | 7202 | Analysis & Design | Project analysis, design review, dependency checks, pattern suggestions |
| **FORGE** | 7203 | Build & Execution | Build execution, test running, pipeline management, file watching |
| **SCRIBE** | 7204 | Documentation & Knowledge | API documentation, code explanation, knowledge search, docs updates |
| **RECON** | 7205 | Review & Security | Diff review, file analysis, pattern finding, security scans, quality reports |
| **NEXUS** | 7206 | Orchestration | Workflow orchestration, task management, agent roster, request aggregation |
| **WATCHTOWER** | 7207 | Monitoring & Alerts | Health monitoring, agent metrics, alert history, restart capability |

## Agent Details

### SENTINEL (Port 7201)

The validation and guardrails agent. SENTINEL enforces design constraints,
validates imports against allowed patterns, checks dependency trees for
conflicts, and runs guardrail rules before changes are committed. Acts as
the first line of defense against non-compliant changes.

### ARCHITECT (Port 7202)

The analysis and design review agent. ARCHITECT performs project-level
analysis, reviews proposed designs against established patterns, checks
dependency health, and suggests architectural improvements. Consulted
before significant structural changes.

### FORGE (Port 7203)

The build execution agent. FORGE manages the full build pipeline: compiling
code, running test suites, managing CI pipelines, and watching for file
changes that trigger rebuild cycles. The primary execution engine for
automated build and test workflows.

### SCRIBE (Port 7204)

The documentation and knowledge agent. SCRIBE generates API documentation,
explains code, searches the knowledge base, and keeps docs in sync with
code changes. Responsible for ensuring documentation drift does not occur.

### RECON (Port 7205)

The code review and security agent. RECON performs diff reviews, analyzes
file changes for patterns, runs security scans, and generates quality
reports. Provides detailed feedback on code health and identifies potential
vulnerabilities.

### NEXUS (Port 7206)

The workflow orchestration agent. NEXUS coordinates multi-step workflows,
manages task queues, maintains the agent roster, and aggregates requests
across agents. Acts as the central dispatcher for complex operations that
span multiple agents.

### WATCHTOWER (Port 7207)

The health monitoring agent. WATCHTOWER monitors all agents and blade
servers for health, collects metrics and performance data, maintains alert
history, and can restart failed services. Provides the aggregated data
that powers the dashboard's sub-agent grid via the `/api/sub-agents` endpoint.

## Implementation Details

### MCP Communication

All agents communicate over the MCP protocol with SSE (Server-Sent Events)
response parsing. Critical implementation notes:

- The MCP SDK returns `text/event-stream`, not raw JSON. Clients must parse SSE frames.
- After the MCP init handshake, agents must send `notifications/initialized`.
- The `Accept` header must include both `application/json` and `text/event-stream`.
- All agents share `blade-client.js` for standardized MCP communication.

### Key Files

| File | Purpose |
| --- | --- |
| `D:\Sub-Agents\lib\blade-client.js` | SSE-aware MCP client shared by all agents |
| `D:\Sub-Agents\lib\agent-base.js` | Shared server factory for agent bootstrapping |
| `D:\Sub-Agents\agents\{name}\index.js` | Individual agent implementation |
| `D:\Sub-Agents\Start-SubAgents.ps1` | Start all 7 agents |
| `D:\Sub-Agents\Stop-SubAgents.ps1` | Stop all 7 agents |

### Resource Usage

| Metric | Value |
| --- | --- |
| Total RAM (7 agents) | ~540 MB |
| Per-agent average | ~77 MB |
| Registration | All registered with blade agent-comm server |
| Aggregation | WATCHTOWER aggregates all data via `/api/sub-agents` |

See also: [Infrastructure](Infrastructure) | [Architecture](Architecture) | [API Reference](API-Reference)
