# Master Control Program API Reference

The platform exposes 25 backend services through the Caddy reverse proxy.
All endpoints are accessible at `http://192.168.1.3:8080/` with path-based
routing. The aggregator gateway at port 7200 unifies all MCP tools into a
single connection point.

## Backend Services

### Blade Tool Servers (18 services)

| Port | Service | Description |
| --- | --- | --- |
| 7101 | **repo-search** | Full-text search across mirrored git repositories |
| 7102 | **docs-search** | Documentation corpus search and retrieval |
| 7103 | **fs-cache** | Filesystem caching for frequently accessed files |
| 7104 | **build-cache** | Build artifact caching and retrieval |
| 7105 | **symbol-index** | Code symbol indexing (functions, classes, definitions) |
| 7106 | **session-context** | Session state persistence across tool calls |
| 7107 | **response-cache** | Response caching for expensive operations |
| 7108 | **git-intel** | Git history analysis, blame, and commit intelligence |
| 7109 | **file-digest** | File hashing, checksums, and change detection |
| 7110 | **vector-search** | Vector embedding search for semantic code matching |
| 7111 | **dep-graph** | Dependency graph analysis and visualization |
| 7112 | **lint-cache** | Lint result caching and retrieval |
| 7113 | **snippet-store** | Reusable code snippet storage and search |
| 7114 | **task-queue** | Task queue management and scheduling |
| 7115 | **memory** | Shared key-value memory with namespace isolation |
| 7116 | **agent-comm** | Inter-agent communication and message routing |
| 7117 | **coordination** | Multi-agent task coordination |
| 7118 | **event-bus** | Event streaming and pub/sub bus |

### Sub-Agents (7 services)

| Port | Agent | Description |
| --- | --- | --- |
| 7201 | **SENTINEL** | Design validation and enforcement guardrails |
| 7202 | **ARCHITECT** | Project analysis and design review |
| 7203 | **FORGE** | Build execution and test pipeline |
| 7204 | **SCRIBE** | Documentation generation and knowledge search |
| 7205 | **RECON** | Code review, security scanning, and quality reports |
| 7206 | **NEXUS** | Workflow orchestration and request aggregation |
| 7207 | **WATCHTOWER** | Health monitoring, metrics, and alerting |

See [Sub-Agents](Sub-Agents) for detailed capability descriptions.

## Dashboard API Endpoints

The browser dashboard consumes these endpoints through Caddy at `:8080`:

### Metrics

| Endpoint | Method | Description |
| --- | --- | --- |
| `/api/metrics` | GET | Current CPU, RAM, network snapshot |
| `/api/metrics/history` | GET | Last 60 data points for charting |
| `/api/metrics/stream` | GET (SSE) | Real-time Server-Sent Events stream for live metrics |

### Service Status

| Endpoint | Method | Description |
| --- | --- | --- |
| `/api/clients` | GET | Connected client list from the client tracker |
| `/api/sub-agents` | GET | Aggregated sub-agent status, uptime, and tool counts (via WATCHTOWER) |

### Communication and Coordination

| Endpoint | Method | Description |
| --- | --- | --- |
| `/api/agent-comm` | GET | Agent-to-agent message flow and communication status |
| `/api/coordination` | GET | Multi-agent task coordination status |
| `/api/event-bus` | GET | Event stream monitoring and recent events |
| `/api/memory-beacon` | GET | Memory server health and beacon status |

## Aggregator Gateway

| Endpoint | Description |
| --- | --- |
| `https://192.168.1.3:8443/mcp/gateway` | HTTPS MCP gateway (self-signed cert) |
| `http://192.168.1.3:8080/mcp/gateway` | HTTP MCP gateway |
| `http://192.168.1.3:7200/health` | Gateway health check |
| `http://192.168.1.3:7200/dashboard` | Gateway built-in dashboard |

The gateway proxies all tool calls to the correct backend. It currently exposes
96+ tools from 21 of 25 online backends. Four services (response-cache,
coordination, event-bus, agent-comm) do not expose MCP tools but are accessible
through their REST API endpoints above.

## Metrics System Details

| Property | Value |
| --- | --- |
| System metrics server | Port 7121 |
| Client tracker | Port 7120 |
| SSE stream | `/api/metrics/stream` (EventSource protocol) |
| Polling interval | 5 seconds for non-SSE endpoints |
| Chart rendering | Custom canvas, 60-point rolling window |
| History depth | 60 data points |

See also: [Architecture](Architecture) | [Infrastructure](Infrastructure)
