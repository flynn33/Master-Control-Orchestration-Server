# Gateway

The MCP Gateway is the single MCOS-advertised endpoint that trusted-LAN clients
use for MCP requests. The current shipping substrate is the in-process
Windows-native HTTP.sys adapter, `NativeHttpSysGatewayAdapter`, behind the
`IMcpGateway` interface.

Source authority:

- `include/MasterControl/MasterControlContracts.h`
- `include/MasterControl/McpGatewayAdapters.h`
- `src/MasterControlApp/McpGatewayAdapters.cpp`
- `src/MasterControlApp/MasterControlDefaults.cpp`
- `include/MasterControl/AdminRouteRegistry.h`

## Current Substrate

| Field | Current behavior |
|---|---|
| Adapter | `NativeHttpSysGatewayAdapter` |
| Process model | In-process inside `MasterControlServiceHost.exe` |
| Gateway type field | `mcpGateway.type` is retained for JSON compatibility; runtime uses the native adapter. |
| External gateway binary | Not required and not supported as the shipping path. |
| Fresh-install enabled state | `mcpGateway.enabled=false` |
| Fresh-install listen host | `127.0.0.1` |
| Default HTTP port | `8080` |
| Default MCP path | `/mcp` |
| Default health path | `/health` |
| Default HTTPS port | `8443` when TLS is enabled and bound |

The default is local-only. Operators can switch to trusted-LAN posture by
patching `mcpGateway.enabled`, `mcpGateway.listenHost`, and related security
fields, then applying firewall rules.

## Enable And Start

```powershell
$patch = @{
  mcpGateway = @{
    enabled = $true
    listenHost = "0.0.0.0"
    mode = "trusted-lan"
  }
  security = @{
    allowOpenLanAccess = $true
    securityPosture = "trusted-lan"
  }
}

Invoke-RestMethod http://localhost:7300/api/config `
  -Method Patch `
  -ContentType "application/json" `
  -Headers @{ "X-Confirm-Unsafe" = "1" } `
  -Body ($patch | ConvertTo-Json -Depth 8)

Restart-Service MasterControlProgram
Invoke-RestMethod http://localhost:7300/api/gateway/start -Method Post
```

## Verify

```powershell
Invoke-RestMethod http://localhost:7300/api/gateway/status | ConvertTo-Json -Depth 4
Invoke-RestMethod http://localhost:7300/api/gateway/health | ConvertTo-Json -Depth 4
Invoke-RestMethod http://localhost:8080/health | ConvertTo-Json
```

MCP initialize smoke test:

```powershell
$init = @{
  jsonrpc = "2.0"
  id = 1
  method = "initialize"
  params = @{
    protocolVersion = "2025-03-26"
    capabilities = @{}
    clientInfo = @{ name = "smoke"; version = "1.0" }
  }
}

Invoke-RestMethod http://localhost:8080/mcp `
  -Method Post `
  -ContentType "application/json" `
  -Body ($init | ConvertTo-Json -Depth 8)
```

Expected indicators:

- `/api/gateway/status` reports native adapter state.
- `GET /health` returns gateway health JSON.
- `POST /mcp` accepts JSON-RPC.
- `GET /mcp` is not the MCP request path; the current native gateway uses
  POST-based MCP requests.

## Transport Contract

The alpha gateway implements the **POST-only Streamable HTTP** subset of MCP:
each client request is a JSON-RPC 2.0 envelope sent with `POST /mcp`, and each
response is returned as a single JSON body. Server-initiated SSE upgrade is not
implemented in this build.

Non-POST requests to `/mcp` return `405 Method Not Allowed` with `Allow: POST`.
Stateful routing honors the standard `Mcp-Session-Id` header, with the
MCOS-specific `X-MCOS-Session-Id` retained as a compatibility fallback.

## HTTP And HTTPS

HTTP and HTTPS are separate binds:

| Surface | Config fields |
|---|---|
| HTTP gateway | `mcpGateway.listenHost`, `listenPort`, `mcpPath`, `healthPath` |
| HTTPS gateway | `mcpGateway.tlsEnabled`, `tlsListenPort`, `tlsCertThumbprint` |

See [TLS and HTTPS](TLS-and-HTTPS) before enabling HTTPS. HTTP.sys certificate
binding is managed by Windows through `netsh http add sslcert`; the runtime uses
the resulting TLS-bound state to decide whether to advertise HTTPS URLs.

## Tool Catalog And Calls

Gateway tools are sourced from supervised worker pools:

```powershell
Invoke-RestMethod http://localhost:7300/api/gateway/tools | ConvertTo-Json -Depth 8
```

Current behavior:

- `tools/list` aggregates tools from ready pool instances.
- Qualified names use `{poolId}__{toolName}`.
- `tools/call` resolves the name, obtains a lease, and forwards the JSON-RPC
  request to the selected worker instance.
- Ambiguous unqualified names are rejected instead of choosing an arbitrary
  backend.
- Session headers support sticky routing for stateful sessions.

## Admin Routes

| Method | Route | Purpose |
|---|---|---|
| `GET` | `/api/gateway/status` | Current gateway status. |
| `GET` | `/api/gateway/health` | Probe/health result. |
| `GET` | `/api/gateway/tools` | Aggregated tool catalog. |
| `POST` | `/api/gateway/start` | Start gateway; requires `network.admin`. |
| `POST` | `/api/gateway/stop` | Stop gateway; requires `network.admin`. |
| `POST` | `/api/gateway/restart` | Restart gateway; requires `network.admin`. |

## Legacy External Gateway

Earlier versions used an external supervised gateway binary. That is historical
context only. The current alpha does not require an external gateway executable,
and the MSI does not install one.

## Related Pages

[Configuration](Configuration) |
[TLS and HTTPS](TLS-and-HTTPS) |
[Worker Pools](Worker-Pools) |
[LAN Discovery](LAN-Discovery) |
[API Reference](API-Reference) |
[Troubleshooting](Troubleshooting)
