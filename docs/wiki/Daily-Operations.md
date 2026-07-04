# Daily Operations

The tasks you perform once MCOS is running. Health checks, adding and removing pools, looking at activity, applying configuration changes.

For first-time install, see [Quick Start](Quick-Start). For maintenance like updates and uninstall, see [Maintenance](Maintenance).

---

## Health check

Run after a host reboot, after a config change, or any time something feels off.

### From the dashboard
Open `http://localhost:7300/` → **Overview**. Five summary cards should render:
- **MCP Gateway**: state and health
- **Worker Pools**: count + ready instances + active leases
- **Connected Clients**: count
- **Governance Posture**: pass / warn / blocked
- **Host Telemetry**: CPU / memory / disk

If any card is empty or shows an error, drill into the matching destination.

### From PowerShell
```powershell
# Service status
Get-Service MasterControlProgram | Format-Table -AutoSize

# Health endpoint (operator surface)
Invoke-RestMethod http://localhost:7300/api/health | ConvertTo-Json

# Discovery doc (what's advertised on the LAN)
Invoke-RestMethod http://localhost:7300/api/discovery | ConvertTo-Json -Depth 6

# Gateway state + health probe (live IMcpGateway::Probe)
Invoke-RestMethod http://localhost:7300/api/gateway/status | ConvertTo-Json
Invoke-RestMethod http://localhost:7300/api/gateway/health | ConvertTo-Json

# Bootstrapper preflight (validates payload + service + firewall + registry)
& "C:\Program Files\Master Control Orchestration Server\MasterControlBootstrapper.exe" preflight --json-output
```

A green health check means: service `Running`, `/api/health` returns `status=ok`, gateway `state` is `running` or `configured`, preflight reports zero issues.

---

## See what AI clients are connected

The presence roster is heartbeat-driven. Clients that POST to `/api/telemetry/heartbeat` show up here.

### From the dashboard
Dashboard → **Clients**. Each row is one client with:
- `clientId`, `clientType`, IP
- Self-reported CPU%, memory%, GPU%, GPU MB
- Bytes sent/received per second
- Last seen timestamp

Cells reading `unavailable` mean the client never reported that metric. Honest by design — never collapsed to `0%`.

### From PowerShell
```powershell
Invoke-RestMethod http://localhost:7300/api/telemetry/clients | Format-Table clientId, clientType, ipAddress, lastSeenUtc -AutoSize
```

If a client you expect is missing, see [Troubleshooting](Troubleshooting) §LAN discovery — the host probably can't reach MCOS.

---

## See what just happened (Activity stream)

Two parallel rings:

### Telemetry events ring
PHASE-08 events. 1024-entry capped ring. Categories: `system` / `gateway` / `worker` / `client` / `discovery` / `governance` × severities: `info` / `warning` / `error` / `critical`.

```powershell
Invoke-RestMethod 'http://localhost:7300/api/telemetry/events?max=50' | ConvertTo-Json -Depth 6
```

Dashboard surface: **Activity** destination → top card.

### Legacy admin activity ring
ADR-001 ring. Operator-surface events (client registration, governance decisions, exports).

```powershell
Invoke-RestMethod http://localhost:7300/api/activity | ConvertTo-Json -Depth 6
```

Dashboard surface: **Activity** destination → bottom card.

---

## Add a managed MCP server pool

```powershell
$body = @{
  poolId        = 'mcos-shell-tools'
  kind          = 'mcp-server'
  logicalMcpUrl = 'http://localhost:18443/mcp/shell'
  template      = @{
    executable       = 'C:\Program Files\my-mcp-shell\my-mcp-shell.exe'
    args             = @('--port', '18443')
    workingDirectory = ''
    environment      = @{}
    transport        = 'streamable_http'
    healthProbe      = @{
      transport          = 'http'
      path               = '/health'
      intervalMs         = 5000
      timeoutMs          = 1500
      unhealthyThreshold = 3
    }
  }
  scalePolicy   = @{
    minInstances              = 0
    maxInstances              = 4
    maxActiveLeasesPerInstance = 8
    scaleOutQueueWaitMs        = 1500
    scaleInIdleSeconds         = 120
  }
  drainPolicy   = @{
    drainStickySessions           = $true
    drainTimeoutSeconds           = 30
    routeNewSessionsToReplacement = $true
  }
} | ConvertTo-Json -Depth 6

Invoke-RestMethod -Method POST `
  -Uri http://localhost:7300/api/pools `
  -ContentType 'application/json' `
  -Body $body
```

`POST /api/pools` is upsert — running it again with the same `poolId` updates the definition.

After registering: dashboard → **Pools** → confirm the new pool card appears with `instanceCount: 0` and `at saturation: no`.

### Force scale to minInstances
```powershell
Invoke-RestMethod -Method POST http://localhost:7300/api/pools/mcos-shell-tools/scale
```
Or click **Scale to min** on the pool card.

The supervisor spawns instances under Job Objects. Each instance transitions `Configured → Starting → Ready` (or `Failed`). Dashboard reflects state in the per-pool card.

For full pool semantics see [Worker Pools](Worker-Pools).

---

## Drain a pool for maintenance

```powershell
Invoke-RestMethod -Method POST http://localhost:7300/api/pools/mcos-shell-tools/drain
```
Or click **Drain** on the pool card.

What happens:
- Every instance transitions to `Draining`.
- Existing sticky leases continue routing to their bound instance.
- New stateless leases route to non-draining Ready instances elsewhere.
- As leases drain to zero, instances transition to `Stopped`.

Hot-migration is forbidden — stateful sessions complete on their original instance. ADR-002 §8 / FORBIDDEN-CONTRACT §2.4.

---

## Remove a pool

```powershell
Invoke-RestMethod -Method POST http://localhost:7300/api/pools/mcos-shell-tools/remove
```

Removing a pool also removes the persisted definition from the configuration file. Existing in-flight leases on running instances complete first; the supervisor reaps the workers under Job Object closure.

---

## See active leases

```powershell
# All pools, all leases
Invoke-RestMethod http://localhost:7300/api/pools | ConvertFrom-Json | ForEach-Object {
    $poolId = $_.poolId
    $leases = Invoke-RestMethod "http://localhost:7300/api/pools/$poolId/leases"
    [pscustomobject]@{ Pool = $poolId; ActiveLeases = $leases.Count }
} | Format-Table -AutoSize

# Saturation snapshot for one pool
Invoke-RestMethod http://localhost:7300/api/pools/mcos-shell-tools/saturation | ConvertTo-Json
```

Dashboard surface: **Pools** destination → expand the **N active leases** disclosure on each card.

---

## Approve / reject a pending governance action

When a governed action would be high-impact, CLU defers it instead of executing immediately. The pending list shows up under Governance.

### From the dashboard
Dashboard → **Governance** → "Pending approvals" card. Each row has Approve / Reject.

### From PowerShell
```powershell
# List pending
Invoke-RestMethod http://localhost:7300/api/clu/approvals |
  Where-Object { $_.status -eq 'pending' } |
  Format-Table id, action, actor, createdAtUtc -AutoSize

# Approve
Invoke-RestMethod -Method POST http://localhost:7300/api/clu/approvals/<id>/approve

# Reject (with reason)
$body = @{ reason = 'Out of policy this quarter' } | ConvertTo-Json
Invoke-RestMethod -Method POST -Uri http://localhost:7300/api/clu/approvals/<id>/reject `
  -Body $body -ContentType 'application/json'
```

See [Governance](Governance) for the full operator approval queue + decision policy.

---

## Hand a client a governance bundle

```powershell
# Three platforms supported
foreach ($p in 'windows','macos','ios') {
    $bundle = Invoke-RestMethod "http://localhost:7300/api/governance/bundles/$p"
    $bundle | ConvertTo-Json -Depth 6 |
        Out-File "$env:USERPROFILE\Desktop\mcos-governance-$p.json" -Encoding utf8
    Write-Host "Saved $p bundle (sha256=$($bundle.checksum))"
}
```

Dashboard surface: **Governance** → "Governance bundles" card has tabs and a download link per platform. See [CLU Governance](CLU-Governance).

---

## Apply a configuration change

If the field is on the WinUI Settings panel, edit there and click Apply Host Settings. Otherwise:

```powershell
# Edit the current configuration file.
notepad "$env:ProgramData\MasterControlOrchestrationServer\config\master-control-orchestration-server.json"

# Restart so the runtime re-reads
Restart-Service MasterControlProgram
```

Some changes hot-apply through `POST /api/config` or a partial `PATCH /api/config` request. Unsafe network posture changes require `X-Confirm-Unsafe: true`. See [Configuration](Configuration) for the field-by-field reference.

---

## Watch logs in real time

Diagnostics logs land under `%PUBLIC%\Documents\Master Control Orchestration Server\logs\runtime\`.

```powershell
$log = "$env:PUBLIC\Documents\Master Control Orchestration Server\logs\runtime\events.jsonl"
Get-Content $log -Wait -Tail 50

# Or filter by category
Get-Content $log -Wait -Tail 0 | Where-Object { $_ -match '"category":"gateway"' }
```

Dashboard equivalent: **Activity** destination polls every 5 seconds.

---

## Cross-references

- **First-time install** → [Quick Start](Quick-Start)
- **Field-by-field config** → [Configuration](Configuration)
- **Pool model in detail** → [Worker Pools](Worker-Pools)
- **Telemetry surface** → [Telemetry and Activity](Telemetry-and-Activity)
- **Governance roles** → [CLU Governance](CLU-Governance) (bundles) + [Governance](Governance) (approvals)
- **Updates and uninstall** → [Maintenance](Maintenance)
- **What to do when something breaks** → [Troubleshooting](Troubleshooting)
