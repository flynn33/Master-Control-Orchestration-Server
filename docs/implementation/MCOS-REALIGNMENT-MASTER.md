# MCOS Realignment Master Architecture

## Final target

MCOS is a Windows-native LAN MCP Gateway host. It does not execute ChatGPT, Codex, Claude Code, Grok, or other AI providers directly. Those tools run on client machines and connect to MCOS over the trusted LAN.

## Core services

| Service | Responsibility |
|---|---|
| LAN Discovery Service | Advertise MCOS via DNS-SD/mDNS and UDP fallback; publish discovery documents. |
| MCP Gateway Service | Provide one MCP endpoint for clients. Preferred initial adapter: MCPJungle. |
| Onboarding Profile Service | Generate model-specific and generic client configuration. |
| CLU Governance Service | Serve Forsetti governance bundles and evaluate governed decisions. |
| Worker Supervisor | Spawn, monitor, restart, drain, and terminate MCP/sub-agent workers. |
| Lease Router | Assign client sessions/requests to worker instances behind stable logical endpoints. |
| Autoscaler | Add or remove worker instances based on utilization and queue pressure. |
| Telemetry Aggregator | Collect host, gateway, client, worker, and activity telemetry. |
| Dashboard | Tron-style real-time operator display. |

## Gateway-first topology

```text
Claude Code / Codex / Grok / Generic MCP Client
  -> DNS-SD or onboarding document discovers MCOS
  -> connects to MCOS MCP Gateway URL
  -> MCPJungle or native gateway aggregates logical tools
  -> MCOS logical pool endpoints
  -> Lease Router
  -> supervised worker instance
```

## MCPJungle role

MCPJungle is a gateway substrate, not the whole product. MCOS supervises it and registers stable logical endpoints. MCOS does not register every autoscaled clone as a separate MCP server.

## LAN trust model

AI-client gateway access has no application-layer authentication. Security is enforced by:

- trusted LAN boundary
- Windows Firewall scoping
- operator-controlled LAN mode enablement
- subnet/interface policy
- strict host/origin validation where compatible with clients
- CLU/Forsetti governance for mutating operations

## Client onboarding

MCOS generates:

- Claude Code profile
- Codex profile
- Grok/xAI profile
- ChatGPT optional connector-edge notes/profile
- generic MCP client profile

Profiles include one gateway URL, governance bundle URL, telemetry heartbeat URL if applicable, and model-specific setup snippets.

## Autoscaling rule

For stateful MCP sessions, do not hot-migrate active in-flight sessions. Drain existing sessions and route new sessions/leases to new instances. Forced retirement may require client reinitialize behavior.

