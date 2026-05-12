# MCOS Realignment Rule

MCOS must be redesigned as a Windows-native LAN MCP Gateway host.

## Old model to remove

- Embedded provider execution.
- Provider assignment as an execution target inside MCOS.
- Per-client app-layer authorization as the core AI-client connection model.
- Direct client-to-worker addressing as the normal path.
- Live-looking seeded infrastructure that is not actually configured, installed, reachable, and healthy.

## New model to implement

- External AI clients run on their own machines.
- MCOS advertises one LAN MCP Gateway.
- Clients consume MCOS-generated onboarding/configuration profiles.
- MCP servers and sub-agents are supervised worker pools behind stable logical endpoints.
- The current and only shipping gateway substrate is the in-process `NativeHttpSysGatewayAdapter` behind `IMcpGateway` (v0.9.0+). MCPJungle was retired at v0.9.0; the historical `McpJungleGatewayAdapter` was removed.
- MCOS owns discovery, governance, telemetry, worker supervision, autoscaling, dashboarding, and Windows packaging.
- Direct AI plugin slots (Claude Code, ChatGPT, Grok) on the Overview deck are mutually exclusive: turning one on revokes the other two (v0.10.12+).

## Required language in docs

Use these terms consistently:

- MCP Gateway
- LAN Discovery Service
- Client Onboarding Profile
- Governance Bundle
- Managed Endpoint Pool
- Endpoint Instance
- Endpoint Lease
- Worker Supervisor
- Lease Router
- Telemetry Aggregator

