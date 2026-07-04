# Configuration

This page documents the current MCOS configuration file, default fields, and
admin API write semantics. Source authority is
`src/MasterControlApp/MasterControlDefaults.cpp`,
`include/MasterControl/MasterControlModels.h`, and
`include/MasterControl/JsonMerge.h`.

## File Locations

Current runtime path:

```text
%ProgramData%\MasterControlOrchestrationServer\config\master-control-orchestration-server.json
```

Legacy fallback path:

```text
%ProgramData%\MasterControlOrchestrationServer\config\master-control-program.json
```

`resolveAppPaths()` also performs a one-shot ProgramData directory migration from
the legacy `MasterControlProgram` directory to the current
`MasterControlOrchestrationServer` directory when only the legacy directory
exists. If that rename fails, the runtime falls back to the legacy directory so
existing installs keep starting.

The data directory can be overridden with `MASTERCONTROL_DATA_DIR`.

Related state paths under the same data directory:

| Path | Purpose |
|---|---|
| `state\install-history.json` | Package/import history |
| `state\apple-operations.json` | Apple remote operation history |
| `state\entitlements.json` | Entitlement state |
| `work\` | Runtime work directory |

## Editing Rules

Prefer the admin API for live changes:

| Route | Semantics |
|---|---|
| `GET /api/config` | Returns the current `AppConfiguration`. |
| `POST /api/config` | Full-document replacement. Omitted top-level fields are rejected as partial documents. |
| `PATCH /api/config` | Partial object-recursive deep merge into the current document. Arrays, scalars, and null replace the target value. |

Use `PATCH /api/config` for partial operator changes:

```powershell
$patch = @{
  instanceName = "mcos-lab-01"
  mcpGateway = @{
    enabled = $true
    listenHost = "0.0.0.0"
    mode = "trusted-lan"
  }
}

Invoke-RestMethod http://localhost:7300/api/config `
  -Method Patch `
  -ContentType "application/json" `
  -Headers @{ "X-Confirm-Unsafe" = "1" } `
  -Body ($patch | ConvertTo-Json -Depth 8)
```

Use `POST /api/config` only when sending a complete configuration document:

```powershell
$cfg = Invoke-RestMethod http://localhost:7300/api/config
$cfg.instanceName = "mcos-lab-01"

Invoke-RestMethod http://localhost:7300/api/config `
  -Method Post `
  -ContentType "application/json" `
  -Body ($cfg | ConvertTo-Json -Depth 16)
```

Unsafe configuration changes require:

```text
X-Confirm-Unsafe: 1
```

The same confirmation header applies to both `POST /api/config` and
`PATCH /api/config` when the runtime classifies the change as unsafe.

Direct file edits are available for recovery, but require a service restart:

```powershell
notepad "$env:ProgramData\MasterControlOrchestrationServer\config\master-control-orchestration-server.json"
Restart-Service MasterControlProgram
```

## Fresh-Install Defaults

`buildDefaultConfiguration()` currently initializes these operator-visible
defaults:

| Field | Default |
|---|---|
| `instanceName` | `Master Control Orchestration Server` |
| `instanceId` | Generated `mcos-...` identifier |
| `bindAddress` | `127.0.0.1` |
| `browserPort` | `7300` |
| `beaconPort` | `7301` |
| `beaconBroadcastIntervalSeconds` | `15` |
| `beaconEnabled` | `false` |
| `aiAutonomyEnabled` | `false` |
| `advancedMode` | `false` |
| `firstRunCompleted` | `false` |
| `setupMode` | `guided` |
| `setupCurrentStep` | `welcome` |

The default posture is local-only. Opening MCOS to a trusted LAN is an explicit
operator action through configuration plus firewall rules.

## `activeProfile`

`activeProfile` describes the host identity MCOS advertises to clients.

Important fields:

| Field | Meaning |
|---|---|
| `environmentName` | Host name plus OS description detected at first run. |
| `preferredBindAddress` | Operator-preferred LAN address for discovery documents and generated client URLs. |
| `macAddress` | Detected primary network MAC address. |
| `seededEndpoints` | Initial endpoint descriptors generated from the preferred address. |

Do not confuse `preferredBindAddress` with listener bind fields. It controls
advertised URLs; listener fields control socket binding.

## `mcpGateway`

`mcpGateway` controls the client-facing MCP gateway.

```json
{
  "type": "native",
  "enabled": false,
  "binaryPath": "",
  "listenHost": "127.0.0.1",
  "listenPort": 8080,
  "mcpPath": "/mcp",
  "healthPath": "/health",
  "databasePath": "",
  "mode": "local-only",
  "tlsEnabled": false,
  "tlsListenPort": 8443,
  "tlsCertThumbprint": ""
}
```

| Field | Current behavior |
|---|---|
| `type` | Deserializes legacy values, but the runtime uses the native HTTP.sys adapter. |
| `enabled` | Controls whether the gateway starts. Fresh installs default to disabled. |
| `binaryPath` | Retained for backward-compatible JSON round-trips; no external gateway binary is required. |
| `listenHost` | HTTP gateway bind host. Fresh installs use `127.0.0.1`; LAN mode commonly uses `0.0.0.0` or a specific LAN IP. |
| `listenPort` | HTTP gateway port. Default `8080`. |
| `mcpPath` | MCP endpoint path. Default `/mcp`. |
| `healthPath` | Gateway health path. Default `/health`. |
| `databasePath` | Retained for backward-compatible JSON round-trips. |
| `mode` | Posture label such as `local-only`, `trusted-lan`, or `hardened-lan`. |
| `tlsEnabled` | Enables the HTTPS HTTP.sys prefix when certificate binding is also configured. |
| `tlsListenPort` | HTTPS gateway port. Default `8443`. |
| `tlsCertThumbprint` | Operator-provided certificate thumbprint used as the signal that HTTPS may be advertised. |

See [Gateway](Gateway) and [TLS and HTTPS](TLS-and-HTTPS).

## `security`

```json
{
  "enableTls": false,
  "enableAuthentication": true,
  "allowTroubleshootingBypass": false,
  "allowOpenLanAccess": false,
  "securityProtocolsEnabled": true,
  "securityPosture": "local-only",
  "trustedRemoteHosts": [],
  "beaconSigningEnabled": true,
  "beaconSigningKey": "<generated on fresh installs>",
  "adminTlsEnabled": false,
  "adminTlsCertThumbprint": ""
}
```

| Field | Current behavior |
|---|---|
| `allowOpenLanAccess` | Operator-controlled LAN exposure flag used by setup and firewall guidance. |
| `securityPosture` | Human-readable posture label. Fresh installs use `local-only`. |
| `beaconSigningEnabled` | Enables HMAC-SHA256 signing when `beaconSigningKey` is present. |
| `beaconSigningKey` | Generated on fresh installs; legacy configs may be empty and then broadcast unsigned. |
| `adminTlsEnabled` | Enables SChannel TLS on the admin listener when a usable cert thumbprint is configured. |
| `adminTlsCertThumbprint` | SHA-1 thumbprint for a certificate in `Cert:\LocalMachine\My`. |

Admin TLS is separate from gateway TLS. `netsh http add sslcert` applies to the
HTTP.sys gateway, not to the Winsock admin listener.

## `resourceAllocation`

```json
{
  "cpuPercent": 50,
  "memoryPercent": 50,
  "bandwidthPercent": 50,
  "storagePercent": 50
}
```

These fields are operator intent for resource allocation. Current worker-pool
supervision and telemetry use the implemented pool model described in
[Worker Pools](Worker-Pools).

## `pools`

Pool definitions are persisted under `pools` and managed through
`GET /api/pools` and `POST /api/pools`. Do not use older field names such as
`executablePath` or `arguments`; the current model uses:

```json
{
  "poolId": "filesystem",
  "kind": "mcp-server",
  "logicalMcpUrl": "http://127.0.0.1:8080/mcp/filesystem",
  "template": {
    "executable": "npx.cmd",
    "args": ["-y", "@modelcontextprotocol/server-filesystem", "D:\\Work"],
    "workingDirectory": "",
    "environment": {},
    "transport": "stdio",
    "healthProbe": {
      "transport": "stdio_handshake",
      "path": "",
      "intervalMs": 5000,
      "timeoutMs": 1500,
      "unhealthyThreshold": 3
    }
  },
  "scalePolicy": {
    "minInstances": 0,
    "maxInstances": 1,
    "maxActiveLeasesPerInstance": 1,
    "scaleOutQueueWaitMs": 1500,
    "scaleInIdleSeconds": 300
  },
  "drainPolicy": {
    "drainTimeoutSeconds": 30,
    "drainStickySessions": true,
    "routeNewSessionsToReplacement": true
  }
}
```

Full pool lifecycle and executable resolution behavior live in
[Worker Pools](Worker-Pools).

## Back Up And Restore

Minimal configuration backup:

```powershell
$src = "$env:ProgramData\MasterControlOrchestrationServer\config\master-control-orchestration-server.json"
$dst = "$env:USERPROFILE\Desktop\mcos-config-$(Get-Date -Format yyyyMMdd-HHmmss).json"
Copy-Item $src $dst
```

Restore:

```powershell
Copy-Item $dst "$env:ProgramData\MasterControlOrchestrationServer\config\master-control-orchestration-server.json" -Force
Restart-Service MasterControlProgram
```

## Related Pages

[Quick Start](Quick-Start) |
[TLS and HTTPS](TLS-and-HTTPS) |
[Gateway](Gateway) |
[Worker Pools](Worker-Pools) |
[Windows Firewall and LAN Mode](Windows-Firewall-LAN-Mode) |
[Troubleshooting](Troubleshooting)
