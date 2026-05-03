# Architecture

![governing](https://img.shields.io/badge/governing-ADR--002-5a00e8?style=flat-square)
![layer](https://img.shields.io/badge/layer-C%2B%2B20%20%E2%80%A2%20WinUI%203%20%E2%80%A2%20vanilla%20JS-00f6ff?style=flat-square)
![phases](https://img.shields.io/badge/realignment-12%2F12%20phases-00aacc?style=flat-square)
![modules](https://img.shields.io/badge/Forsetti%20modules-16-1cf2c1?style=flat-square)

Canonical map of how MCOS is structured. When in doubt, the source files referenced here are ground truth — every assertion on this page points to a real header, route, or test.

The architecture target is the **gateway-first MCP host** declared in [ADR-002](Architecture-Decisions/ADR-002-gateway-first-mcp-realignment) and locked at the substrate level by [ADR-003](Architecture-Decisions/ADR-003-mcp-gateway-substrate-decision). The original [ADR-001 LAN client identity model](Architecture-Decisions/ADR-001-lan-client-control-plane) survives as the operator surface that coexists with the AI-client gateway surface.

---

## 1. Two surfaces, one host

MCOS is a single Windows service hosting two logically distinct surfaces.

```mermaid
flowchart LR
    classDef accent fill:#031018,stroke:#00F6FF,color:#E6FCFF,stroke-width:2px;
    classDef sub fill:#0a1018,stroke:#5A00E8,color:#8CB7C4;
    classDef good fill:#031a14,stroke:#1cf2c1,color:#a8efe0;

    subgraph AIClients[AI clients on the LAN]
        AC1[Claude Code, Codex,<br/>Grok, ChatGPT, generic MCP]
    end

    subgraph Operator[Operator]
        OP1[Browser dashboard<br/>+ WinUI shell]
    end

    subgraph Host[Master Control Orchestration Server <br/>single Windows service]
        AIClientSurface[AI-client surface<br/>auth=none, trust=lan]:::accent
        OperatorSurface[Operator surface<br/>LAN-trusted with privilege gates]:::accent
    end

    AIClients -->|"MCP Gateway URL<br/>(advertised via DNS-SD)"| AIClientSurface
    Operator -->|"Admin API + dashboard<br/>(per-client privilege gates)"| OperatorSurface

    AIClientSurface -.-> AIClientSurface_Note[/no app-layer auth /<br/>trust at network layer/]:::sub
    OperatorSurface -.-> OperatorSurface_Note[/X-MCOS-Client-Id middleware /<br/>nine-flag privilege model/]:::sub
```

The AI-client surface is gateway-first (PHASE-02 onward). The operator surface preserves the ADR-001 LAN client identity model with `X-MCOS-Client-Id` and per-client privileges. Network firewall scoping (`Profile=Private,Domain`) is the load-bearing trust control on both surfaces.

---

## 2. Runtime topology

```mermaid
flowchart TB
    classDef accent fill:#031018,stroke:#00F6FF,color:#E6FCFF,stroke-width:2px;
    classDef sub fill:#0a1018,stroke:#5A00E8,color:#8CB7C4;
    classDef client fill:#031827,stroke:#5AE8FF,color:#A8DCFF;
    classDef good fill:#031a14,stroke:#1cf2c1,color:#a8efe0;

    subgraph LANClients[LAN AI clients on remote hosts]
        ClaudeCode[/Claude Code on host A/]:::client
        Codex[/Codex on host B/]:::client
        Other[/Generic MCP on host C/]:::client
    end

    subgraph Surfaces[Operator surfaces on the host]
        Browser[Browser dashboard<br/><code>resources/web/</code>]:::accent
        Shell[WinUI 3 shell<br/><code>src/MasterControlShell/</code>]:::accent
    end

    subgraph Host[Host process]
        Service[[<b>MasterControlServiceHost.exe</b><br/>Windows service entry point]]:::accent
        Runtime[(<b>MasterControlRuntime</b><br/>shared in-process core)]:::accent
    end

    subgraph Services[Runtime services]
        direction TB
        Disc[<b>DiscoveryService</b><br/>Win32 DnsServiceRegister]:::accent
        Beacon[<b>BeaconService</b><br/>UDP discovery doc broadcast]:::accent
        Onboard[<b>OnboardingProfileService</b>]:::accent
        Gov[<b>GovernanceBundleService</b>]:::accent
        Sup[<b>WorkerSupervisor</b><br/>Job Object containment]:::accent
        Lease[<b>LeaseRouter</b><br/>sticky / least-loaded]:::accent
        Tel[<b>TelemetryAggregator</b>]:::accent
        Adapter[(<b>McpJungleGatewayAdapter</b><br/>or future native gateway)]:::good
        Pools[(<b>Managed Endpoint Pools</b>)]:::good
    end

    subgraph External[External processes supervised under Job Objects]
        Jungle[(MCPJungle binary<br/><code>operator-installed</code>)]:::sub
        Workers[(MCP server / sub-agent<br/>backend instances)]:::sub
    end

    LANClients -->|"MCP requests (HTTP /mcp)"| Adapter
    LANClients -.->|"DNS-SD discovery"| Disc
    LANClients -.->|"GET /.well-known/mcos.json"| Disc
    LANClients -.->|"GET /api/onboarding/{type}"| Onboard
    LANClients -.->|"GET /api/governance/bundles/{platform}"| Gov

    Browser --> Service
    Shell --> Service
    Service --> Runtime
    Runtime --> Disc
    Runtime --> Beacon
    Runtime --> Onboard
    Runtime --> Gov
    Runtime --> Sup
    Runtime --> Lease
    Runtime --> Tel
    Runtime --> Adapter
    Adapter ==>|"supervises"| Jungle
    Sup ==>|"spawns + supervises"| Workers
    Adapter -->|"registers logical pools"| Sup
    Lease -->|"route requests to"| Pools
    Sup -->|"manages"| Pools
    Pools -.->|"heartbeats"| Tel
```

---

## 3. The eleven required terms

ADR-002 §1 fixes the vocabulary. Use these terms exactly throughout the codebase, the docs, and operator communication:

| Term | Meaning | Lives in |
|---|---|---|
| **MCP Gateway** | The single MCOS-advertised MCP endpoint AI clients connect to | `IMcpGateway` / `McpJungleGatewayAdapter` |
| **LAN Discovery Service** | DNS-SD + UDP beacon advertising the gateway URL | `DiscoveryService` |
| **Client Onboarding Profile** | Per-client-type config bundle MCOS hands out at first connect | `IOnboardingProfileService` + `/api/onboarding/{clientType}` |
| **Governance Bundle** | Forsetti + agentic-coding instructions per platform | `IGovernanceBundleService` + `/api/governance/bundles/{platform}` |
| **Managed Endpoint Pool** | A group of MCP-server or sub-agent instances under one supervisor | `ManagedEndpointPool` |
| **Endpoint Instance** | A single supervised backend in a pool | `EndpointInstance` |
| **Endpoint Lease** | One client-to-instance binding | `EndpointLease` |
| **Worker Supervisor** | Spawns and reaps pool instances under Windows Job Objects | `WorkerSupervisor` |
| **Lease Router** | Selects an instance per request (sticky or least-loaded) | `LeaseRouter` |
| **Telemetry Aggregator** | Events ring + client roster + gateway traffic snapshot | `TelemetryAggregator` |
| **(IMcpGateway adapter)** | The replaceable C++ interface that abstracts the gateway substrate | `IMcpGateway` |

If a doc or code path uses a different term for any of these, that is a drift to flag.

---

## 4. Layered architecture

```mermaid
flowchart TB
    classDef A fill:#031018,stroke:#00F6FF,color:#E6FCFF,stroke-width:2px;
    classDef B fill:#0a1018,stroke:#5A00E8,color:#A8DCFF,stroke-width:2px;
    classDef C fill:#031a14,stroke:#1cf2c1,color:#a8efe0,stroke-width:2px;
    classDef D fill:#1a0f00,stroke:#FFA500,color:#FFE6BF,stroke-width:2px;

    subgraph L1[Layer 1 - Surfaces]
      direction LR
      DashboardL1[Browser dashboard<br/>11 destinations]:::A
      ShellL1[WinUI shell<br/>+ Settings panel]:::A
      AdminAPI[Admin API<br/>JSON over HTTP]:::A
      AIClientAPI[AI-client gateway HTTP<br/>+ DNS-SD]:::A
    end

    subgraph L2[Layer 2 - Coordination services]
      direction LR
      DiscSvc[Discovery]:::B
      OnboardSvc[Onboarding profiles]:::B
      GovSvc[Governance bundles]:::B
      LeaseSvc[Lease router]:::B
      TelSvc[Telemetry aggregator]:::B
    end

    subgraph L3[Layer 3 - Supervision and substrate]
      direction LR
      Supervisor[Worker Supervisor]:::C
      GatewayAdapter[IMcpGateway adapter]:::C
      JobObj[Windows Job Objects]:::D
    end

    subgraph L4[Layer 4 - External processes]
      direction LR
      JungleBin[MCPJungle binary<br/>operator-installed]:::D
      Backends[Backend MCP servers and sub-agents<br/>spawned by supervisor]:::D
    end

    L1 --> L2
    L2 --> L3
    L3 --> L4
```

---

## 5. Request flow — AI client first connect

```mermaid
sequenceDiagram
    autonumber
    participant Client as AI client
    participant DNS as DNS-SD on the LAN
    participant Disc as DiscoveryService
    participant Onboard as OnboardingProfileService
    participant Gateway as IMcpGateway
    participant Pool as Managed pool
    participant Lease as LeaseRouter

    Client->>DNS: Browse _mcos._tcp.local
    DNS-->>Client: PTR -> mcos-<instance>
    Client->>Disc: GET /.well-known/mcos.json
    Disc-->>Client: DiscoveryDocument (gateway URL, governance URL,<br/>onboarding paths, auth=none, trust=lan)
    Client->>Onboard: GET /api/onboarding/{clientType}
    Onboard-->>Client: OnboardingProfile<br/>(config snippet + manual instructions)

    note over Client: Apply config snippet

    Client->>Gateway: MCP request (POST /mcp)
    Gateway->>Lease: acquireLease(poolId, sessionId, stateful)
    Lease->>Lease: 1 sticky lookup<br/>2 least-loaded ready<br/>3 scale-out if at saturation<br/>4 fail honestly
    Lease->>Pool: bind lease to instance N
    Lease-->>Gateway: EndpointLease(active)
    Gateway->>Pool: forward MCP request to instance N
    Pool-->>Gateway: response
    Gateway-->>Client: response
```

The lease router's four-step selection rule is locked in PHASE-07 and FORBIDDEN-CONTRACT §2.4. Hot-migration of stateful streams is forbidden — once a session has a lease, it sticks.

---

## 6. Endpoint instance lifecycle

The seven states from PHASE-06.

```mermaid
stateDiagram-v2
    [*] --> Configured: pool registered
    Configured --> Starting: scale-up trigger or operator action
    Starting --> Ready: probe says healthy
    Starting --> Failed: spawn / probe failure
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

Implementation: `EndpointInstanceState` enum in `include/MasterControl/MasterControlModels.h`. Test pinning: `testEndpointInstanceStateAllSevenLifecycleStates` in `tests/MasterControlOrchestrationServerTests.cpp`.

---

## 7. Honest telemetry

ADR-002 §9 forbids fabricated telemetry. Every numeric metric in `ClientHeartbeat` and `WorkerTelemetry` defaults to `-1.0` ("unavailable"); `0.0` is reserved for genuine "idle" readings (PDH-direct host metrics only).

```mermaid
flowchart LR
    classDef good fill:#031a14,stroke:#1cf2c1,color:#a8efe0;
    classDef bad fill:#1a0a0a,stroke:#ff7a90,color:#ffd2d8;
    classDef neutral fill:#0a1018,stroke:#5A00E8,color:#A8DCFF;

    subgraph Source[Source of metric]
        ClientReports[Client heartbeat]:::neutral
        WorkerProbe[Worker probe]:::neutral
        PDH[PDH host counters]:::neutral
    end

    subgraph Default[Default value if unreported]
        Sentinel[-1.0 unavailable]:::good
        IdleZero[0.0 genuine idle]:::good
        Fabricated[Fake number]:::bad
    end

    ClientReports -->|"missing field?"| Sentinel
    WorkerProbe -->|"probe never ran?"| Sentinel
    PDH -->|"directly measured"| IdleZero

    Fabricated -.->|"forbidden"| X[FORBIDDEN-CONTRACT 4.1 / 4.2 / 4.3 / 8.1]:::bad
```

The dashboard's `formatMetric()` helper (in `resources/web/app.js`) renders `-1.0` as the literal string `unavailable`, never as `0%`. The same rule applies to the WinUI shell. FORBIDDEN-CONTRACT §8.1 enforces.

---

## 8. The 12 phases of the realignment

ADR-002 was delivered in 12 explicitly labeled phases. Every phase has its own file in `handoff/realignment/` plus a completion report.

```mermaid
gantt
    title MCOS Realignment Program (PHASE-00..PHASE-11)
    dateFormat  YYYY-MM-DD
    axisFormat  %m-%d

    section Foundation
    PHASE-00 Repo baseline + ADR-002       :done, p0, 2026-04-30, 1d
    PHASE-01 Provider-era removal          :done, p1, after p0, 1d

    section Gateway plus discovery
    PHASE-02 MCP Gateway spike (MCPJungle) :done, p2, after p1, 1d
    PHASE-03 Bonjour LAN discovery         :done, p3, after p2, 1d

    section Onboarding plus governance
    PHASE-04 Onboarding profiles           :done, p4, after p3, 1d
    PHASE-05 CLU/Forsetti governance bundles :done, p5, after p4, 1d

    section Worker fabric
    PHASE-06 Managed worker pools          :done, p6, after p5, 1d
    PHASE-07 Autoscaling + lease routing   :done, p7, after p6, 1d
    PHASE-08 Real-time telemetry           :done, p8, after p7, 1d

    section UI and release
    PHASE-09 Tron dashboard realignment    :done, p9, after p8, 1d
    PHASE-10 Windows hardening, CI, MSI    :done, p10, after p9, 1d
    PHASE-11 Native gateway evaluation     :done, p11, after p10, 1d
```

Each phase ended with a written completion report. See [Versions](Versions) for the full timeline + commit SHAs.

---

## 9. Source layout

| Directory | Purpose |
|---|---|
| `include/MasterControl/` | Public C++ headers — interfaces and models |
| `src/MasterControlApp/` | Runtime core: `MasterControlRuntime`, `McpGatewayAdapters`, `MasterControlModels`, `MasterControlDefaults` |
| `src/MasterControlBootstrapper/` | Setup / install / preflight logic invoked by the MSI |
| `src/MasterControlServiceHost/` | Windows service host (`--console` mode for dev) |
| `src/MasterControlShell/` | WinUI 3 desktop shell |
| `src/MasterControlModules/` | Forsetti module registrations |
| `resources/web/` | Browser dashboard (HTML + vanilla JS + CSS) |
| `resources/clu/` | CLU governance profile JSON |
| `resources/icons/` | App icon set + MSI banner / dialog bitmaps |
| `installer/` | WiX v5 source for the MSI |
| `scripts/` | Build, package, sync, compliance, deployment scripts |
| `Forsetti-Framework-Windows-main/` | Vendored Forsetti framework — sealed by ADR-002 §11 |
| `tests/` | C++ tests (`MasterControlOrchestrationServerTests.cpp`) |
| `docs/wiki/` | Operator-facing documentation (mirror of the GitHub wiki) |
| `docs/implementation/` | Internal architecture, schemas, drift inventory, FORBIDDEN-CONTRACT |
| `handoff/realignment/` | Phase manifests + phase files + completion reports |

---

## 10. Build pipeline

```mermaid
flowchart LR
    classDef step fill:#031018,stroke:#00F6FF,color:#E6FCFF;
    classDef gate fill:#031a14,stroke:#1cf2c1,color:#a8efe0;

    A[VERSION.json bump]:::step
    A --> B[Sync README badges + vcpkg.json]:::step
    B --> C[cmake --preset release]:::step
    C --> D[cmake --build build/release --config Release]:::step
    D --> E[ctest --test-dir build/release]:::gate
    E --> F[Forsetti compliance script]:::gate
    F --> G[mcos-contracts grep audit]:::gate
    G --> H[Package-MasterControlOrchestrationServer.ps1]:::step
    H --> I[Build-Msi.ps1 via WiX v5]:::step
    I --> J[MSI artifact + bootstrapper preflight]:::gate
    J --> K[Tag + GitHub Release via release.yml]:::gate
```

The same pipeline runs in CI via `.github/workflows/windows-build-test-package.yml`. Releases gate on a successful same-SHA gate run; see [Release Gate](Operations/Release-Gate).

---

## 11. Configuration

`mcos.json` lives at `%ProgramData%\Master Control Orchestration Server\mcos.json` after install. Operators edit it directly or via the WinUI Settings panel. Key fields:

```json
{
  "instanceId": "mcos-<uuid>",
  "instanceName": "Master Control Orchestration Server",
  "bindAddress": "0.0.0.0",
  "browserPort": 7300,
  "beaconPort": 7301,
  "beaconEnabled": true,
  "mcpGateway": {
    "type": "mcpjungle",
    "enabled": false,
    "binaryPath": "",
    "listenHost": "0.0.0.0",
    "listenPort": 8080,
    "mcpPath": "/mcp",
    "healthPath": "/health",
    "mode": "lan-trusted"
  },
  "security": { "allowOpenLanAccess": false },
  "resourcePolicy": {
    "cpuAllocationPercent": 50,
    "memoryAllocationPercent": 50,
    "bandwidthAllocationPercent": 100,
    "storageAllocationPercent": 50
  }
}
```

Default values come from `buildDefaultConfiguration()` in `src/MasterControlApp/MasterControlDefaults.cpp`. The schema is documented in `docs/implementation/`.

---

## 12. Cross-references

- **Decisions** → [Architecture Decisions](Architecture-Decisions)
- **Gateway substrate** → [Gateway](Gateway)
- **Pools and leases** → [Worker Pools](Worker-Pools)
- **Discovery wire format** → [LAN Discovery](LAN-Discovery)
- **Telemetry surface** → [Telemetry and Activity](Telemetry-and-Activity)
- **HTTP routes** → [API Reference](API-Reference)
- **Dashboard tour** → [Dashboard](Dashboard)
- **Operator surface (ADR-001)** → [LAN Clients](LAN-Clients), [Privileges](Privileges), [Client Config Bundle](Client-Config-Bundle)
