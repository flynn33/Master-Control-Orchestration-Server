# Master Control Program Infrastructure

The entire BLADE MCP infrastructure runs on a single physical server with
Caddy as the reverse proxy, 26 backend services, and a self-signed TLS
certificate for remote access.

## Server Hardware

| Property | Value |
| --- | --- |
| Hostname | MASTER-CONTROL |
| IP Address | `192.168.1.3` |
| OS | Windows Server 2022 Datacenter |
| CPU | 2x Intel Xeon E5-2640 (12 cores / 24 threads) |
| RAM | 64 GB |
| Storage | 280 GB (C:) system + 10 TB (D:) data |

## Network Topology

```
  Remote Clients (LAN)
        |
   +----v-----------------------------------------+
   |  Caddy Reverse Proxy                         |
   |  :8080 (HTTP)  :8443 (HTTPS, self-signed)    |
   +----+---------+---------+----------+----------+
        |         |         |          |
   /dashboard  /mcp/gw  /api/*    /mcp/blade/*
        |         |         |          |
   Node :18000  GW :7200  Metrics  Individual
                  |       :7120-21  blade routes
                  |                 :7101-7118
                  |
            +-----+------+
            |            |
       Blades        Sub-Agents
       :7101-18      :7201-07
```

## Port Allocation

### Blade Tool Servers (7101-7118)

| Port | Server | Purpose |
| --- | --- | --- |
| 7101 | repo-search | Repository code search |
| 7102 | docs-search | Documentation corpus search |
| 7103 | fs-cache | Filesystem cache |
| 7104 | build-cache | Build artifact cache |
| 7105 | symbol-index | Symbol and definition indexing |
| 7106 | session-context | Session context persistence |
| 7107 | response-cache | Response caching |
| 7108 | git-intel | Git history and intelligence |
| 7109 | file-digest | File hashing and digest |
| 7110 | vector-search | Vector embedding search |
| 7111 | dep-graph | Dependency graph analysis |
| 7112 | lint-cache | Lint result caching |
| 7113 | snippet-store | Reusable code snippet storage |
| 7114 | task-queue | Task queue management |
| 7115 | memory | Shared key-value memory (namespaced) |
| 7116 | agent-comm | Agent-to-agent communication |
| 7117 | coordination | Task coordination |
| 7118 | event-bus | Event stream bus |

### Sub-Agents (7201-7207)

| Port | Agent | Role |
| --- | --- | --- |
| 7201 | SENTINEL | Validation and guardrails |
| 7202 | ARCHITECT | Analysis and design review |
| 7203 | FORGE | Build execution and pipelines |
| 7204 | SCRIBE | Documentation and knowledge |
| 7205 | RECON | Code review and security |
| 7206 | NEXUS | Workflow orchestration |
| 7207 | WATCHTOWER | Health monitoring and alerts |

See [Sub-Agents](Sub-Agents) for detailed descriptions.

### Infrastructure Services

| Port | Service | Purpose |
| --- | --- | --- |
| 7200 | Aggregator Gateway | Single-endpoint proxy for all 96+ tools |
| 7120 | Client Tracker | Connected client tracking and LAN broadcast |
| 7121 | System Metrics | CPU, RAM, network telemetry |
| 18000 | Dashboard Server | Node.js static server for the browser dashboard |
| 8080 | Caddy HTTP | HTTP reverse proxy |
| 8443 | Caddy HTTPS | HTTPS reverse proxy (self-signed cert) |

## Caddy Reverse Proxy

Caddy handles all external routing with two listeners:

- **Port 8080 (HTTP)**: Routes to individual blade servers, the dashboard,
  metrics, sub-agents, and the aggregator gateway via path-based routing.
- **Port 8443 (HTTPS)**: Self-signed certificate (`CN=master-control`,
  IP SAN `192.168.1.3`, thumbprint `9A2DC81D`). Primarily used for the
  aggregator gateway endpoint at `/mcp/gateway`.

The Caddyfile lives at `D:\mcp\config\Caddyfile`.

## Data Storage

All persistent data is stored under `D:\mcp\data\`:

| Directory | Content |
| --- | --- |
| `memory/` | Shared key-value memory (namespaced per client) |
| `sessions/` | Session context persistence |
| `repos/` | Mirrored git repositories |
| `docs/` | Documentation corpus |
| `indexes/` | Symbol and vector indexes |
| `snippets/` | Reusable code patterns |
| `build-cache/` | Build artifacts |
| `lint-cache/` | Lint results |
| `response-cache/` | Response cache |

See also: [Architecture](Architecture) | [Sub-Agents](Sub-Agents) | [Remote Client](Remote-Client)
