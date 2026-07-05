# Worker Pools

![phase](https://img.shields.io/badge/landed-PHASE--06%20%2B%20PHASE--07-00aacc?style=flat-square)
![supervision](https://img.shields.io/badge/containment-Job%20Object-1cf2c1?style=flat-square)
![lifecycle](https://img.shields.io/badge/states-7-00f6ff?style=flat-square)
![routing](https://img.shields.io/badge/routing-sticky%20%2B%20least--loaded-5a00e8?style=flat-square)

The fabric MCOS supervises behind the gateway. A **Managed Endpoint Pool** is a group of MCP-server or sub-agent instances under one supervisor with a stable logical URL. The lease router picks an instance per incoming request — sticky for stateful sessions, least-loaded for stateless — and triggers same-type scale-out when every Ready instance is at capacity.

---

## How to add a managed pool

Use `POST /api/pools` from PowerShell or the dashboard. The body is the full `ManagedEndpointPool` JSON; field-by-field reference is in [Configuration](Configuration) § `pools`.

```powershell
$body = @{
  poolId        = 'mcos-shell-tools'
  kind          = 'mcp-server'
  logicalMcpUrl = 'http://0.0.0.0:8080/mcp/shell'
  template      = @{
    executable       = 'C:\Program Files\my-mcp-shell\my-mcp-shell.exe'
    args             = @('--port', '18443')
    workingDirectory = ''
    environment      = @{}
    transport        = 'stdio'   # or 'streamable_http'
  }
  scalePolicy   = @{
    minInstances               = 1
    maxInstances               = 4
    maxActiveLeasesPerInstance = 8
    scaleOutQueueWaitMs        = 1500
    scaleInIdleSeconds         = 120
  }
} | ConvertTo-Json -Depth 6

Invoke-RestMethod -Method POST -Uri http://localhost:7300/api/pools `
  -ContentType 'application/json' -Body $body
```

`POST /api/pools` is upsert. Re-running the same body modifies the existing pool definition.

**Two-step contract.** Registering a pool stores its definition only — it does **not** auto-spawn instances. Force the supervisor to honor `minInstances` with an explicit follow-up:
```powershell
Invoke-RestMethod -Method POST http://localhost:7300/api/pools/mcos-shell-tools/scale
```

The supervisor spawns instances under Job Objects. Each transitions `Configured → Starting → Ready` (or `Failed`). Confirm via dashboard → **Pools**, or:

```powershell
Invoke-RestMethod http://localhost:7300/api/pools/mcos-shell-tools |
  ConvertTo-Json -Depth 6
```

### Field guidance — JSON shape that the runtime actually accepts

The wire shape uses these field names verbatim. Earlier copies of this page had drifted (e.g. `executablePath`, `arguments`) — the runtime accepts only the names below.

| Field | When to set what |
|---|---|
| `kind` | `mcp-server` for generic backends; `sub-agent` for purpose-built specialized backends |
| `logicalMcpUrl` | Stable URL for this pool. The lease router routes requests to one instance behind it. |
| `template.executable` | Worker executable. Three accepted forms: an **absolute path** (used only if it exists), a **relative path containing a directory separator** (resolved against the service working directory), or a **bare command name** such as `npx.cmd` or `node`, resolved via Windows PATH search (`SearchPathW`) with `.exe`/`.cmd`/`.bat` probing when no extension is given. Resolution failures fail the instance with a diagnostic naming the executable. |
| `template.args` | Array of command-line args passed at spawn. For `.cmd`/`.bat` workers MCOS launches through `cmd.exe /s /c` (required for spaced install paths); args containing cmd control characters (`&` `\|` `<` `>` `^`) are **rejected** with a clear instance diagnostic because cmd would interpret them as command syntax. `%VAR%` text is allowed but remains subject to cmd expansion. |
| `template.workingDirectory` | CWD for the spawned process |
| `template.environment` | Object — extra env vars merged onto the inherited block |
| `template.transport` | `stdio` or `streamable_http` |
| `scalePolicy.minInstances` | `0` for opt-in (manual scale only); `1` to keep one warm; higher for hot-warm baselines |
| `scalePolicy.maxInstances` | Upper bound — the supervisor will not exceed this even under saturation |
| `scalePolicy.maxActiveLeasesPerInstance` | When every Ready instance hits this, the next lease triggers same-type scale-out |
| `scalePolicy.scaleOutQueueWaitMs` | Max time the lease router waits before triggering scale-out under saturation |
| `scalePolicy.scaleInIdleSeconds` | Idle threshold before an excess instance is reclaimed |
| `drainPolicy.drainStickySessions` (default `true`) | If `true`, lets sticky leases finish on a draining instance before reaping it |
| `drainPolicy.drainTimeoutSeconds` (default `30`) | Hard ceiling on graceful drain |
| `drainPolicy.routeNewSessionsToReplacement` (default `true`) | New stateful sessions skip the Draining instance |
| `template.healthProbe.path` | HTTP path for `http` probe transports; ignored for stdio |
| `template.healthProbe.transport` | `http`, `stdio_handshake`, or `none` |
| `template.healthProbe.intervalMs` (default `5000`) | How often the supervisor probes the worker |
| `template.healthProbe.timeoutMs` (default `1500`) | Per-probe timeout |
| `template.healthProbe.unhealthyThreshold` (default `3`) | Consecutive failures before the instance is marked Failed |

---

## How to scale, drain, and remove

### Scale to minInstances
```powershell
Invoke-RestMethod -Method POST http://localhost:7300/api/pools/<poolId>/scale
```
Or click **Scale to min** on the pool card.

### Drain
```powershell
Invoke-RestMethod -Method POST http://localhost:7300/api/pools/<poolId>/drain
```
Or click **Drain** on the pool card.

What happens:
- Every instance moves to `Draining`.
- Existing sticky leases keep routing to their bound instance until they release.
- New stateless leases route to non-draining Ready instances elsewhere.
- As leases drain to zero, instances move to `Stopped`.

Hot-migration is forbidden — a stateful session never gets re-bound to a different instance mid-flight.

### Remove
```powershell
Invoke-RestMethod -Method POST http://localhost:7300/api/pools/<poolId>/remove
```
Removes the pool definition from the configuration file. The supervisor reaps any running instances under Job Object closure.

---

## How to inspect lease activity

```powershell
# Active leases on a pool
Invoke-RestMethod http://localhost:7300/api/pools/<poolId>/leases |
  Format-Table leaseId, sessionId, instanceId, state, acquiredAtUtc -AutoSize

# Saturation snapshot
Invoke-RestMethod http://localhost:7300/api/pools/<poolId>/saturation |
  ConvertTo-Json
```

Saturation flags:
| Flag | Meaning | Operator action |
|---|---|---|
| `atSaturation: true` | Every Ready instance hit `maxActiveLeasesPerInstance` | Watch — next request triggers scale-out |
| `scaleOutTriggered: true` | At least one scale-up happened during the saturation window | None — observable signal |
| `atMaxInstances: true` | Pool can't grow further; new leases fail honestly | Either bump `maxInstances` or accept the failure |

Dashboard surface: each pool card on the **Pools** destination renders these flags inline.

---

## How to acquire / release a lease manually

Most operators don't do this — the gateway acquires leases on behalf of AI client requests automatically. Useful for testing.

```powershell
# Acquire — the runtime takes sessionId + sessionType. sessionType=stateful
# binds the session sticky to the instance for the lifetime of the lease;
# sessionType=stateless lets the next call route to whatever's least-loaded.
$body = @{
  sessionId   = 'test-sess-1'
  sessionType = 'stateful'
} | ConvertTo-Json
$lease = Invoke-RestMethod -Method POST `
  -Uri http://localhost:7300/api/pools/mcos-shell-tools/leases `
  -ContentType 'application/json' -Body $body
$lease | ConvertTo-Json

# Release
Invoke-RestMethod -Method POST "http://localhost:7300/api/leases/$($lease.leaseId)/release"
```

If the pool is at `maxInstances` and saturated, the lease comes back with `state: failed` and a status message — that's the honest-fail behavior from the four-step rule.

---

## Reference

### 1. Type model

```mermaid
classDiagram
    class ManagedEndpointPool {
        +string poolId
        +EndpointPoolKind kind
        +string displayName
        +string logicalMcpUrl
        +EndpointTemplate template
        +ScalePolicy scalePolicy
        +DrainPolicy drainPolicy
        +vector~EndpointInstance~ instances
        +string createdAtUtc
        +string updatedAtUtc
        +string quarantinedUntilUtc
    }

    class EndpointTemplate {
        +string executable
        +vector~string~ args
        +string workingDirectory
        +map~string,string~ environment
        +string transport
        +HealthProbeSpec healthProbe
    }

    class ScalePolicy {
        +int minInstances
        +int maxInstances
        +int maxActiveLeasesPerInstance
        +int scaleOutQueueWaitMs
        +int scaleInIdleSeconds
    }

    class DrainPolicy {
        +int drainTimeoutSeconds
        +bool drainStickySessions
        +bool routeNewSessionsToReplacement
    }

    class HealthProbeSpec {
        +string transport
        +string path
        +int intervalMs
        +int timeoutMs
        +int unhealthyThreshold
    }

    class EndpointInstance {
        +string instanceId
        +string poolId
        +EndpointInstanceState state
        +uint32 processId
        +bool supervised
        +string startedAtUtc
        +string lastTransitionAtUtc
        +string statusMessage
        +WorkerTelemetry telemetry
    }

    class EndpointLease {
        +string leaseId
        +string poolId
        +string instanceId
        +string sessionId
        +LeaseState state
        +string acquiredAtUtc
        +string releasedAtUtc
        +string statusMessage
        +string clientIpAddress
        +string clientType
    }

    class WorkerTelemetry {
        +int activeLeases
        +int queueDepth
        +int inflightCalls
        +double cpuPercent
        +double memoryMbytes
        +string lastProbedAtUtc
        +string lastHealthMessage
    }

    ManagedEndpointPool *-- EndpointTemplate
    ManagedEndpointPool *-- ScalePolicy
    ManagedEndpointPool *-- DrainPolicy
    EndpointTemplate *-- HealthProbeSpec
    ManagedEndpointPool *-- "*" EndpointInstance
    EndpointInstance *-- WorkerTelemetry
    EndpointLease ..> EndpointInstance : binds to
```

All types live in [`include/MasterControl/MasterControlModels.h`](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/include/MasterControl/MasterControlModels.h). JSON shape conforms to [`docs/implementation/schemas/managed-endpoint-pool.schema.json`](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/docs/implementation/schemas/managed-endpoint-pool.schema.json).

The ManagedEndpointPool struct uses `template_` as the C++ field name (avoiding the reserved word) and maps it to the schema's `template` JSON key via explicit `to_json` / `from_json`. This is a documented gotcha — see `mcos-memory.get_file_annotations` for the file-level note.

---

## 2. Lifecycle (seven states)

```mermaid
stateDiagram-v2
    [*] --> Configured: pool registered<br/>(POST /api/pools)
    Configured --> Starting: scaleUp trigger or operator action
    Starting --> Ready: probe says healthy
    Starting --> Failed: spawn or probe failure
    Ready --> Busy: lease acquired
    Busy --> Ready: lease released
    Ready --> Draining: drainPool()
    Busy --> Draining: drainPool()
    Draining --> Stopped: lease count = 0
    Failed --> Stopped: terminal
    Ready --> Stopped: shutdownAll()
    Busy --> Stopped: shutdownAll()
    Stopped --> [*]
```

The seven states (`Configured`, `Starting`, `Ready`, `Busy`, `Draining`, `Failed`, `Stopped`) are the contract from PHASE-06. `testEndpointInstanceStateAllSevenLifecycleStates` pins each slug.

---

## 3. Worker Supervisor — process containment

The supervisor spawns each instance under a Windows Job Object so the host's lifecycle reaps the worker tree atomically.

```mermaid
sequenceDiagram
    autonumber
    participant Op as Operator
    participant Supervisor as WorkerSupervisor
    participant Win32 as Windows kernel
    participant Job as Job Object
    participant Worker as Worker process

    Op->>Supervisor: POST /api/pools/{id}/scale
    Supervisor->>Win32: CreateJobObjectW
    Supervisor->>Job: SetInformationJobObject(KILL_ON_JOB_CLOSE)
    Supervisor->>Win32: CreateProcessW(template.exe, CREATE_SUSPENDED)
    Win32-->>Supervisor: PROCESS_INFORMATION
    Supervisor->>Job: AssignProcessToJobObject
    Supervisor->>Win32: ResumeThread

    note over Supervisor,Worker: state -> Starting

    loop Health probe every healthProbe.intervalSeconds
        Supervisor->>Worker: GET healthProbe.path
        Worker-->>Supervisor: 200 OK
    end

    note over Supervisor: state -> Ready

    note over Supervisor,Worker: When MCOS shuts down,<br/>closing hJobObject<br/>kills the worker tree.
```

Source: `WorkerSupervisor::startInstanceLocked` in [`src/MasterControlApp/MasterControlRuntime.cpp`](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/src/MasterControlApp/MasterControlRuntime.cpp). FORBIDDEN-CONTRACT §2.1a forbids `CreateProcessW` outside this site and the gateway adapter.

---

## 4. Lease router — four-step selection

The lease router is the request-time gate between the gateway and the pool. PHASE-07 / FORBIDDEN-CONTRACT §2.4 lock its rule.

```mermaid
flowchart TB
    classDef step fill:#031018,stroke:#00F6FF,color:#E6FCFF;
    classDef good fill:#031a14,stroke:#1cf2c1,color:#a8efe0;
    classDef warn fill:#1a0f00,stroke:#FFA500,color:#FFE6BF;
    classDef bad fill:#1a0a0a,stroke:#ff7a90,color:#ffd2d8;

    Start([acquireLease<br/>poolId, sessionId, stateful])
    Start --> S1{Step 1<br/>Sticky lookup<br/>sessionId in map?}:::step
    S1 -->|hit, lease still active| Return1[Return existing lease<br/>NEVER re-bind]:::good
    S1 -->|miss| S2{Step 2<br/>Least-loaded ready instance<br/>activeLeases &lt; max?}:::step
    S2 -->|found| BindLeast[Bind new lease to that instance]:::good
    S2 -->|all at capacity| S3{Step 3<br/>scaleUpOnce poolId<br/>under maxInstances?}:::step
    S3 -->|yes| Spawn[Supervisor spawns instance<br/>state -> Starting]:::warn
    Spawn --> Wait[Probe loop transitions Ready]
    Wait --> BindNew[Bind new lease to fresh instance]:::good
    S3 -->|no, at maxInstances| S4[Step 4<br/>Fail honestly<br/>LeaseState = Failed]:::bad

    BindLeast --> Done([Active EndpointLease])
    BindNew --> Done
    Return1 --> Done
```

| Step | What it does |
|---|---|
| **Step 1 — sticky** | If a `sessionId` already maps to an active lease, return that lease verbatim. Hot-migration to a different instance is forbidden by FORBIDDEN-CONTRACT §2.4. |
| **Step 2 — least-loaded** | Among Ready instances (Draining is skipped), pick the one with fewest active leases under `maxActiveLeasesPerInstance`. |
| **Step 3 — scale-out** | If every Ready instance is at capacity and the pool is below `maxInstances`, call `scaleUpOnce(poolId)`. Bind the new lease to the freshly spawned instance. |
| **Step 4 — fail honestly** | At `maxInstances` with everyone saturated, return `LeaseState::Failed` with a clear status message. ADR-002 §9: do not invent capacity that does not exist. |

---

## 5. Saturation observability

The dashboard's Pools panel renders one card per pool with utilization, lease count, and saturation flags. Backed by `/api/pools/{poolId}/saturation`:

```json
{
  "poolId": "mcos-shell-tools",
  "instanceCount": 3,
  "readyInstanceCount": 2,
  "drainingInstanceCount": 1,
  "activeLeaseCount": 5,
  "queueDepth": 0,
  "atSaturation": true,
  "scaleOutTriggered": true,
  "atMaxInstances": false
}
```

Three boolean flags drive operator attention:

| Flag | Means | Color in dashboard |
|---|---|---|
| `atSaturation` | Every Ready instance is at `maxActiveLeasesPerInstance` | Warning (yellow) |
| `scaleOutTriggered` | At least one scale-up happened during the saturation window | Info (blue) |
| `atMaxInstances` | Pool can no longer grow; new leases get `LeaseState::Failed` | Bad (red) |

`queueDepth` is honest: it is `0` until PHASE-08 telemetry feeds real backpressure data into the field. ADR-002 §9.

---

## 6. Drain semantics

`drainPool()` marks every instance in the pool `Draining`. The lease router skips Draining instances when picking least-loaded — but **existing sticky leases stay**. New stateful sessions route to non-draining instances. Existing stateful sessions complete on their original instance.

```mermaid
sequenceDiagram
    autonumber
    participant Op as Operator
    participant Sup as WorkerSupervisor
    participant Lease as LeaseRouter
    participant InstD as Draining instance
    participant InstR as Ready instance

    Op->>Sup: POST /api/pools/{id}/drain
    Sup->>InstD: state = Draining
    Sup-->>Op: 200 (drain initiated)

    note over Lease,InstD: Existing sticky leases on InstD<br/>continue to route there

    par Existing session keeps using InstD
        InstD->>InstD: process requests for sticky leases
    and New session arrives
        Op->>Lease: acquireLease(new session)
        Lease->>InstR: bind new lease (skips InstD)
    end

    note over InstD: As leases drain to zero,<br/>instance transitions to Stopped.
```

Stateful clients are not yanked. Stateless clients transition gracefully.

---

## 7. HTTP routes

| Method | Route | Purpose |
|---|---|---|
| `GET` | `/api/pools` | All pools |
| `GET` | `/api/pools/{poolId}` | One pool with current instances |
| `POST` | `/api/pools` | Upsert pool definition |
| `POST` | `/api/pools/{poolId}/scale` | Force ensure `minInstances` |
| `POST` | `/api/pools/{poolId}/drain` | Drain |
| `POST` | `/api/pools/{poolId}/remove` | Remove pool definition |
| `POST` | `/api/pools/{poolId}/leases` | Acquire a lease |
| `POST` | `/api/leases/{leaseId}/release` | Release a lease |
| `GET` | `/api/pools/{poolId}/leases` | Active leases on this pool |
| `GET` | `/api/pools/{poolId}/saturation` | Saturation snapshot |

The dashboard's Pools panel (PHASE-09) consumes all of these.

---

## 8. Default policy is safe

`buildDefaultConfiguration()` creates **no pools** by default. Operators must explicitly register a pool via `POST /api/pools` before any backend runs. This is ADR-002 §9: "no fake live infrastructure."

`testManagedPoolEmptyByDefault` pins this — empty list of pools when the runtime first comes up; nothing auto-spawns.

---

## 9. Cross-references

- **Gateway integration (single logical server per pool)** → [Gateway](Gateway) §5
- **Sticky-session contract** → [ADR-002 §8](ADR-002-gateway-first-mcp-realignment) + FORBIDDEN-CONTRACT §2.4
- **Pool admin via dashboard** → [Dashboard](Dashboard) §Pools
- **Honest queueDepth** → ADR-002 §9, [Telemetry and Activity](Telemetry-and-Activity)
- **Schema** → [`docs/implementation/schemas/managed-endpoint-pool.schema.json`](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/docs/implementation/schemas/managed-endpoint-pool.schema.json)

---

## 10. Verified working examples (npx-based MCP servers)

The official Model Context Protocol reference servers from `@modelcontextprotocol/*` install through `npx` on demand and run as stdio MCP servers — no API keys, no separate install, no config beyond a working directory. The recipes below have been exercised end-to-end against a fresh MCOS install and confirmed to spawn under the supervisor and accept leases.

**Prerequisites:**
- Node.js on PATH (the supervisor will spawn `C:\Program Files\nodejs\npx.cmd`).
- The Windows Firewall rules from [Windows Firewall and LAN Mode](Windows-Firewall-LAN-Mode) (the four `Master Control Orchestration Server - *` rules a default MSI install creates, or the `MCOS *` rules if applied via the manual fallback snippets).

**Recipe — register all four official MCP servers as `mcp-server` pools, then trigger spawn:**
```powershell
$base = 'http://localhost:7300'
$npx  = 'C:\Program Files\nodejs\npx.cmd'
$cwd  = $env:USERPROFILE

$pools = @(
  @{ id='mcp-filesystem';          pkg='@modelcontextprotocol/server-filesystem';          extraArgs=@("$env:USERPROFILE\Documents") },
  @{ id='mcp-memory';              pkg='@modelcontextprotocol/server-memory';              extraArgs=@() },
  @{ id='mcp-everything';          pkg='@modelcontextprotocol/server-everything';          extraArgs=@() },
  @{ id='mcp-sequential-thinking'; pkg='@modelcontextprotocol/server-sequential-thinking'; extraArgs=@() }
)

foreach ($p in $pools) {
  $body = @{
    poolId        = $p.id
    kind          = 'mcp-server'
    logicalMcpUrl = "http://0.0.0.0:8080/mcp/$($p.id -replace '^mcp-','')"
    template      = @{
      executable       = $npx
      args             = @('-y', $p.pkg) + $p.extraArgs
      workingDirectory = $cwd
      environment      = @{}
      transport        = 'stdio'
    }
    scalePolicy   = @{
      minInstances               = 1
      maxInstances               = 2
      maxActiveLeasesPerInstance = 4
      scaleOutQueueWaitMs        = 1500
      scaleInIdleSeconds         = 180
    }
  } | ConvertTo-Json -Depth 6
  Invoke-RestMethod "$base/api/pools" -Method POST -Body $body -ContentType 'application/json'
  Invoke-RestMethod "$base/api/pools/$($p.id)/scale" -Method POST -Body '{}' -ContentType 'application/json'
}
```

Wait ~30 seconds for the first `npx -y` to pull the package, then read the roster:
```powershell
Invoke-RestMethod "$base/api/pools" |
  ForEach-Object { foreach ($i in $_.instances) { "{0,-25} {1,-12} pid={2}" -f $_.poolId, $i.state, $i.processId } }
```

Each instance should land in `state=ready` with a Win32 PID.

**Recipe — same packages registered as `sub-agent` pools (lease semantics differ; same protocol underneath):**
```powershell
$subagents = @(
  @{ id='subagent-coordinator'; pkg='@modelcontextprotocol/server-everything';            role='coordinator'   },
  @{ id='subagent-memorian';    pkg='@modelcontextprotocol/server-memory';                role='memory-keeper' },
  @{ id='subagent-thinker';     pkg='@modelcontextprotocol/server-sequential-thinking';   role='thinker'       }
)
foreach ($s in $subagents) {
  $body = @{
    poolId        = $s.id
    kind          = 'sub-agent'
    logicalMcpUrl = "http://0.0.0.0:8080/agents/$($s.id -replace '^subagent-','')"
    template      = @{
      executable       = $npx
      args             = @('-y', $s.pkg)
      workingDirectory = $cwd
      environment      = @{ AGENT_ROLE = $s.role }
      transport        = 'stdio'
    }
    scalePolicy   = @{
      minInstances               = 1
      maxInstances               = 2
      maxActiveLeasesPerInstance = 2
      scaleOutQueueWaitMs        = 2500
      scaleInIdleSeconds         = 180
    }
  } | ConvertTo-Json -Depth 6
  Invoke-RestMethod "$base/api/pools" -Method POST -Body $body -ContentType 'application/json'
  Invoke-RestMethod "$base/api/pools/$($s.id)/scale" -Method POST -Body '{}' -ContentType 'application/json'
}
```

**Smoke-test the lease router after spawn:**
```powershell
foreach ($id in @('mcp-filesystem','mcp-memory','mcp-everything','mcp-sequential-thinking',
                  'subagent-coordinator','subagent-memorian','subagent-thinker')) {
  $body = @{ sessionId = "smoke-$id"; sessionType = 'stateless' } | ConvertTo-Json
  $lease = Invoke-RestMethod "$base/api/pools/$id/leases" -Method POST -Body $body -ContentType 'application/json'
  "{0,-25}  leaseId={1}  -> instance={2}" -f $id, $lease.leaseId, $lease.instanceId
}
```

If every line prints a `leaseId` and a corresponding `instanceId`, the supervisor + lease router stack is healthy end-to-end. AI clients consuming the gateway path (`http://<host>:8080/mcp/...` or `/agents/...`) inherit the same routing once the in-process HTTP.sys adapter (or any future native gateway) is bound to TCP 8080 — see [Packaging and Gateway Binary](Packaging-and-Gateway-Binary) for the gateway-binary side.

**Cleanup any test pool:**
```powershell
Invoke-RestMethod "$base/api/pools/<poolId>/drain" -Method POST -Body '{}' -ContentType 'application/json'
# wait a few seconds for instances to reach Stopped
Invoke-RestMethod "$base/api/pools/<poolId>/remove" -Method POST -Body '{}' -ContentType 'application/json'
```
