# MCP Gateway and LAN Discovery Rule

## Gateway-first rule

Do not require each client to discover or configure every backend MCP server. MCOS advertises one gateway endpoint and clients connect to that endpoint.

Preferred first implementation:

- Supervise MCPJungle as an external child process.
- Wrap MCPJungle behind `IMcpGateway` / `McpJungleGatewayAdapter`.
- Register MCOS stable logical endpoints into MCPJungle.
- Let MCOS route stable logical endpoints to supervised worker pools.

## Do not expose autoscaled clones directly

Registering every spawned worker clone as a separate public MCP tool namespace will pollute client tool lists and break the gateway abstraction.

Correct topology:

```text
AI client
  -> MCOS advertised MCP Gateway
  -> MCPJungle /mcp
  -> MCOS logical pool endpoint
  -> MCOS Lease Router
  -> selected backend worker instance
```

## LAN discovery

Advertise MCOS with DNS-SD/mDNS plus optional UDP JSON beacon.

Suggested DNS-SD service types:

- `_mcos._tcp.local`
- `_mcos-mcp._tcp.local`
- `_mcos-onboarding._tcp.local`

Suggested TXT fields:

- `product=MCOS`
- `role=mcp-gateway`
- `gateway=mcpjungle` or `gateway=native`
- `mcp_path=/mcp`
- `config_path=/api/onboarding`
- `governance_path=/api/governance/bundles`
- `protovers=2025-03-26`
- `auth=none`
- `trust=lan`
- `clu=true`
- `forsetti=true`

