# Master Control Orchestration Server — Sub-Agents

![count](https://img.shields.io/badge/count-7-00f6ff?style=flat-square) ![ports](https://img.shields.io/badge/ports-7201--7207-00aacc?style=flat-square) ![transport](https://img.shields.io/badge/transport-MCP%20/%20SSE-5a00e8?style=flat-square)

The sub-agent fleet is a set of seven specialized Node services that ride on top 
of the orchestration server. Each one owns a single concern and registers with the 
runtime through `/api/runtime/subagents`.

---

## Roster

| Agent | Port | Specialization |
| --- | --- | --- |
| **SENTINEL** | `7201` | Design validation, import checking, dependency validation, guardrails |
| **ARCHITECT** | `7202` | Project analysis, design review, dependency checks, pattern suggestions |
| **FORGE** | `7203` | Build execution, test running, pipeline management, file watching |
| **SCRIBE** | `7204` | API documentation, code explanation, knowledge search, docs updates |
| **RECON** | `7205` | Diff review, file analysis, pattern finding, security scans, quality reports |
| **NEXUS** | `7206` | Workflow orchestration, task management, agent roster, request aggregation |
| **WATCHTOWER** | `7207` | Health monitoring, agent metrics, alert history, restart capability |

Combined RAM footprint: ~540 MB total (~77 MB per agent).

---

## Transport

The agents communicate over **MCP** with **SSE** response framing. The shared client 
lives at `D:\Sub-Agents\lib\platform-gateway-client.js` and is reused by every agent.

Critical implementation notes (these are easy to get wrong):

- The `Accept` header **must** include both `application/json` and `text/event-stream`.
- After the MCP `init` handshake, the client **must** send `notifications/initialized`.
- Responses are streamed as `text/event-stream`, not single JSON bodies — parse SSE frames.

---

## Lifecycle

```powershell
# Start the fleet
powershell -NoProfile -ExecutionPolicy Bypass -File D:\Sub-Agents\Start-SubAgents.ps1

# Stop the fleet
powershell -NoProfile -ExecutionPolicy Bypass -File D:\Sub-Agents\Stop-SubAgents.ps1
```

Each agent registers with the orchestration server's agent-communication endpoint, 
and `WATCHTOWER` aggregates the rest via `/api/sub-agents`.

---

## Registration via the runtime

```bash
curl -X POST http://127.0.0.1:7300/api/runtime/subagents \
  -H "Content-Type: application/json" \
  -d '{
    "id": "sentinel",
    "displayName": "SENTINEL",
    "kind": "sub_agent",
    "host": "127.0.0.1",
    "port": 7201,
    "protocol": "http",
    "specialization": "guardrails",
    "userDefined": false
  }'
```

---

See also: [Architecture](Architecture) · [API Reference](API-Reference) · 
[CLU Governance](CLU-Governance)
