# Configuration

The single source of truth for runtime behavior is `mcos.json`, written to `%ProgramData%\Master Control Orchestration Server\` on first run. Edit it from the WinUI Settings panel, the dashboard, or directly.

This page documents every field, when to change it, and how to apply the change.

---

## Where the file lives

| Path | Notes |
|---|---|
| `%ProgramData%\Master Control Orchestration Server\mcos.json` | Operator-edited config |
| `%ProgramData%\Master Control Orchestration Server\runtime\` | Runtime state, logs |
| `C:\Program Files\Master Control Orchestration Server\` | Installed binaries |

`%ProgramData%` is typically `C:\ProgramData`. The directory is created by the MSI on install and is preserved across upgrades.

---

## How to edit configuration

Three ways, in order of preference:

### 1. WinUI Settings panel (interactive)

Start menu → Master Control Orchestration Server → **Settings**. Editable fields:

- Instance name
- Bind address
- Browser port (operator surface)
- Beacon port
- Beacon enabled (toggle)
- Resource allocation: CPU %, memory %, bandwidth %, storage %

Click **Apply Host Settings** to persist. The shell calls `POST /api/config` against the local admin API — service stays up.

### 2. Browser dashboard (PHASE-09 Settings link is a future item — for now use the WinUI shell or the API directly)

You can hit the admin API yourself:

```powershell
# Read current config
Invoke-RestMethod http://localhost:7300/api/config | ConvertTo-Json -Depth 6

# Write a single field
$body = @{ instanceName = 'mcos-eng-lab-1' } | ConvertTo-Json
Invoke-RestMethod -Method POST -Uri http://localhost:7300/api/config -Body $body -ContentType 'application/json'
```

### 3. Direct file edit (for fields the UI does not expose)

```powershell
notepad "$env:ProgramData\Master Control Orchestration Server\mcos.json"
# After saving, restart the service so the new config is read:
Restart-Service MasterControlOrchestrationServer
```

The Settings UI exposes the fields most operators change. Everything else (gateway substrate config, security flags, advanced policy) is only via direct file edit or the API.

---

## Fields, in full

The defaults come from `buildDefaultConfiguration()` in `src/MasterControlApp/MasterControlDefaults.cpp`. A blank `mcos.json` gets every field filled with the default.

### Top level

```json
{
  "instanceId": "mcos-<uuid>",
  "instanceName": "Master Control Orchestration Server",
  "bindAddress": "0.0.0.0",
  "browserPort": 7300,
  "beaconPort": 7301,
  "beaconEnabled": true
}
```

| Field | Default | When to change |
|---|---|---|
| `instanceId` | UUID-backed `mcos-<uuid>` | Override to a recognizable name like `mcos-eng-lab-1`. Stable across restarts. Used as the DNS-SD instance label so multiple hosts on the same LAN don't collide. |
| `instanceName` | "Master Control Orchestration Server" | Human-friendly name shown in the dashboard header and the WinUI shell. |
| `bindAddress` | `0.0.0.0` (all interfaces) | Set to a specific LAN-facing IP if the host is multi-homed and you want MCOS bound to a single NIC. |
| `browserPort` | `7300` | Change if 7300 conflicts. Operator dashboard + admin API live here. |
| `beaconPort` | `7301` | Change if 7301 conflicts. Legacy UDP discovery beacon. |
| `beaconEnabled` | `true` | Disable if your LAN has many hosts and the beacon broadcast is noisy. DNS-SD is independent and stays on. |

### `mcpGateway`

The MCP gateway substrate. Controls the AI-client-facing surface.

```json
"mcpGateway": {
  "type": "mcpjungle",
  "enabled": false,
  "binaryPath": "",
  "listenHost": "0.0.0.0",
  "listenPort": 8080,
  "mcpPath": "/mcp",
  "healthPath": "/health",
  "databasePath": "",
  "mode": "lan-trusted"
}
```

| Field | Default | When to change |
|---|---|---|
| `type` | `mcpjungle` | Future option `native` activates the conditional PHASE-12 native HTTP.sys gateway. ADR-003 keeps `mcpjungle` for v0.6.x. |
| `enabled` | `false` | Set to `true` once you have configured a `binaryPath` AND want MCOS to spawn it. With `enabled=true` and an empty `binaryPath`, MCOS runs in supervised-mock mode and reports `health=unknown` honestly. |
| `binaryPath` | empty | Absolute path to the MCPJungle binary on this host. Required for live gateway behavior. See [Packaging](Packaging-and-Gateway-Binary). |
| `listenHost` | `0.0.0.0` | What host MCPJungle binds. Same multi-homed considerations as `bindAddress`. |
| `listenPort` | `8080` | The MCP gateway port. AI clients connect here. Change if 8080 is taken. The MSI's firewall rule and the dashboard auto-template against this value. |
| `mcpPath` | `/mcp` | Path under the listen host where MCP requests land. Almost never changes. |
| `healthPath` | `/health` | Path the adapter probes via WinHTTP every N seconds. Almost never changes. |
| `databasePath` | empty | If MCPJungle wants a local store, set its path here. Optional. |
| `mode` | `lan-trusted` | Reserved for future use. Documents the trust posture. |

### `security`

```json
"security": { "allowOpenLanAccess": false }
```

| Field | Default | When to change |
|---|---|---|
| `allowOpenLanAccess` | `false` | When `true`, the MSI's firewall integration adds an inbound rule for the operator surface (browser port). When `false`, no operator-surface inbound rule is added — useful if you only want the host reachable via loopback for testing. The AI-client gateway rule is independent. |

### `resourcePolicy`

Policy hint for any worker that asks the runtime "how much should I take?". Today the runtime does not enforce these — they document operator intent.

```json
"resourcePolicy": {
  "cpuAllocationPercent": 50,
  "memoryAllocationPercent": 50,
  "bandwidthAllocationPercent": 100,
  "storageAllocationPercent": 50,
  "enforce": false
}
```

| Field | Default | When to change |
|---|---|---|
| `cpuAllocationPercent` | `50` | Documented hint of what fraction of CPU the worker pools should use. |
| `memoryAllocationPercent` | `50` | Documented hint. |
| `bandwidthAllocationPercent` | `100` | Documented hint. |
| `storageAllocationPercent` | `50` | Documented hint. |
| `enforce` | `false` | Reserved. When `true`, future work would translate the percentages into Job Object resource limits. Keep `false` for now. |

### `pools` (managed endpoint pools)

Pools register via `POST /api/pools` rather than via direct edit. The runtime persists pools to `mcos.json` so they survive restarts.

```json
"pools": [
  {
    "poolId": "mcos-shell-tools",
    "kind": "mcp-server",
    "logicalMcpUrl": "http://localhost:18443/mcp/shell",
    "template": {
      "transport": "streamable_http",
      "executablePath": "C:\\Program Files\\...\\my-mcp-shell.exe",
      "arguments": [],
      "environment": {},
      "workingDirectory": ""
    },
    "scalePolicy": {
      "minInstances": 0,
      "maxInstances": 4,
      "maxActiveLeasesPerInstance": 8
    },
    "drainPolicy": {
      "gracefulSeconds": 30,
      "forceTerminateOnTimeout": true
    },
    "healthProbe": {
      "path": "/health",
      "intervalSeconds": 10,
      "timeoutMs": 1500
    }
  }
]
```

See [Worker Pools](Worker-Pools) for the full model and the `POST /api/pools` flow.

### Other top-level fields

A few legacy fields from ADR-001 are still honored on the operator surface — `clients[]`, `clientConfigBundles[]`, etc. Most operators do not edit these by hand; the dashboard's LAN Identity surface manages them.

---

## How to apply changes

| Scope of change | What to do |
|---|---|
| Anything in `instance*`, `bind*`, `*Port`, `beacon*`, `security`, `resourcePolicy` | Apply via WinUI Settings or the dashboard. The runtime hot-reads after the API call. |
| `mcpGateway.*` | Edit `mcos.json` directly. Restart the service so the gateway adapter rebuilds with the new config. |
| Adding / removing a pool | Use `POST /api/pools` (via dashboard or curl). Live. |
| Changing a pool's scale policy | `POST /api/pools` upserts the existing pool definition. |
| Changing fundamental things (`instanceId`, anything ports-related across the gateway) | Restart the service and re-apply firewall rules with the new ports. |

```powershell
# Restart command for changes that need the service to re-read mcos.json:
Restart-Service MasterControlOrchestrationServer
```

---

## Backup and restore

Pre-realignment best practice carries forward: `mcos.json` is the single source of truth for runtime config. A complete backup is just:

```powershell
$backupPath = "$env:USERPROFILE\Desktop\mcos-backup-$(Get-Date -Format 'yyyyMMdd-HHmmss').json"
Copy-Item "$env:ProgramData\Master Control Orchestration Server\mcos.json" $backupPath
Write-Host "Saved to: $backupPath"
```

To restore, copy the backup back into place and restart the service. See [Maintenance](Maintenance) §Backup for a fuller backup that includes runtime state, logs, and operator-edited governance profile content.

---

## Where to next

- **Setting it up the first time** → [Quick Start](Quick-Start)
- **Adding a worker pool** → [Worker Pools](Worker-Pools) §How to add
- **Firewall rules for these ports** → [Windows Firewall and LAN Mode](Windows-Firewall-LAN-Mode)
- **Backups, upgrades, repair** → [Maintenance](Maintenance)
- **Field-by-field schema source** → `src/MasterControlApp/MasterControlDefaults.cpp`
