# Master Control Program Wiki

Master Control Program is a Forsetti-compliant Windows control plane that manages MCP servers,
AI sub-agents, host telemetry, and browser-based operations from a single platform.
The system runs on the MASTER-CONTROL server (`192.168.1.3`) and exposes 96+ tools
through an aggregator gateway that any Claude Code instance on the LAN can reach with
one connection.

## Current Release

| Field | Value |
| --- | --- |
| Version | `v0.1.48` |
| Released | `2026-03-29` |
| Summary | Automated patch release for Master Control Program. |

## Platform at a Glance

| Component | Count | Details |
| --- | --- | --- |
| Blade tool servers | 18 | Ports 7101-7118, specialized MCP tool providers |
| AI sub-agents | 7 | Ports 7201-7207, autonomous AI workers |
| Aggregator gateway | 1 | Port 7200, single-endpoint proxy for all tools |
| Exposed tools | 96+ | Accessible via HTTPS at `192.168.1.3:8443` |
| Dashboard sections | 7 | Real-time metrics, server grid, agent grid, comms, coordination, events, beacon |

## Wiki Pages

| Page | Description |
| --- | --- |
| [Architecture](Architecture) | System design, dashboard layout, aggregator gateway, and repository module map |
| [Infrastructure](Infrastructure) | Server hardware, port allocation, Caddy proxy, network topology, and storage layout |
| [Sub-Agents](Sub-Agents) | The 7 AI sub-agents, their roles, ports, capabilities, and implementation details |
| [API Reference](API-Reference) | All 25 backend services, API endpoints, metrics system, and health checks |
| [Operations](Operations) | Build, test, install commands, service management, dashboard access, and push guard |
| [Remote Client](Remote-Client) | How to connect a remote Claude Code instance to the BLADE gateway |
| [Automation](Automation) | GitHub agents, CI/CD pipeline, commit conventions, and workflow triggers |
| [Versions](Versions) | Release history, versioning scheme, and release documents |

## Quick Links

- Dashboard: `http://192.168.1.3:8080/dashboard/`
- Gateway health: `http://192.168.1.3:7200/health`
- HTTPS gateway: `https://192.168.1.3:8443/mcp/gateway`
- Repository: https://github.com/flynn33/master-control-dashboard
