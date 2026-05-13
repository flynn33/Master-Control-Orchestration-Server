# MCP Gateway and LAN Discovery Rule

## Gateway-first rule

Do not require each client to discover or configure every backend MCP server. MCOS advertises one gateway endpoint and clients connect to that endpoint.

Current implementation (v0.9.0+):

- Native Win32 HTTP.sys listener inside `MasterControlServiceHost.exe`.
- Wrap the listener behind `IMcpGateway` / `NativeHttpSysGatewayAdapter`.
- Register MCOS stable logical endpoints into the in-process gateway router.
- Let MCOS route stable logical endpoints to supervised worker pools.

Any new gateway adapter must implement `IMcpGateway` and be wired through `cfg.mcpGateway.type` so the topology below stays single-endpoint to clients.

## Do not expose autoscaled clones directly

Registering every spawned worker clone as a separate public MCP tool namespace will pollute client tool lists and break the gateway abstraction.

Correct topology:

```text
AI client
  -> MCOS advertised MCP Gateway
  -> NativeHttpSysGatewayAdapter (cfg.mcpGateway.listenPort + cfg.mcpGateway.mcpPath)
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
- `gateway=native`
- `mcp_path=/mcp`
- `config_path=/api/onboarding`
- `governance_path=/api/governance/bundles`
- `protovers=2025-03-26`
- `auth=none`
- `trust=lan`
- `clu=true`
- `forsetti=true`

