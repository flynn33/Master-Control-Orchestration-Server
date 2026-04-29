# Master Control Orchestration Server — Architecture

![layer](https://img.shields.io/badge/layer-C%2B%2B20%20%E2%80%A2%20WinUI%203%20%E2%80%A2%20vanilla%20JS-00f6ff?style=flat-square)
![phase](https://img.shields.io/badge/rebuild-9%2F9%20phases-00aacc?style=flat-square)
![adr](https://img.shields.io/badge/governing-ADR--001-5a00e8?style=flat-square)
![modules](https://img.shields.io/badge/Forsetti%20modules-16-1cf2c1?style=flat-square)

The canonical map of how the runtime, the operator surfaces, and the Forsetti modules fit together. This page mirrors the actual repository — when in doubt, the source files referenced here are the ground truth.

The architecture target is the **LAN client control plane** described in [ADR-001](Architecture-Decisions/ADR-001-lan-client-control-plane). External AI clients connect over the LAN, identify by header, and operate on a shared MCP and sub-agent fabric under per-client privileges enforced by CLU.

---

## 1. Runtime topology

```mermaid
flowchart TB
    classDef accent fill:#031018,stroke:#00F6FF,color:#E6FCFF,stroke-width:2px;
    classDef sub fill:#0a1018,stroke:#5A00E8,color:#8CB7C4;
    classDef client fill:#031827,stroke:#5AE8FF,color:#A8DCFF;
    classDef good fill:#031a14,stroke:#1cf2c1,color:#a8efe0;

    subgraph Clients[LAN Clients on remote hosts]
        direction LR
        ClaudeCode[/Claude Code on host A/]:::client
        Codex[/Codex on host B/]:::client
        Other[/Any AI agent on host C/]:::client
    end

    subgraph Surfaces[Operator Surfaces]
        direction LR
        Browser[Browser Admin UI<br/><code>resources/web</code>]:::accent
        Shell[WinUI 3 Shell<br/><i>deferred</i>]:::sub
    end

    subgraph Host[Host Process]
        Service[[MasterControlServiceHost.exe<br/>Windows service entry point]]:::accent
        Runtime[(MasterControlRuntime<br/>shared in-process core)]:::accent
    end

    subgraph Forsetti[Forsetti Module Catalog — 16 modules]
        direction TB
        ProtectedRow[Protected:<br/>Configuration • Runtime Inventory<br/>CLU • Dashboard UI]:::accent
        IdentityRow[Identity:<br/>LAN Client Access]:::good
        PlatformRow[Platforms:<br/>Windows / macOS / iOS gateways<br/>Windows / macOS / iOS governance]:::sub
        SupportRow[Support:<br/>Environment Discovery • Host Telemetry<br/>Installer Import • Export • Beacon Gateway]:::sub
    end

    Clients -- "X-MCOS-Client-Id" --> Service
    Browser --> Service
    Shell --> Service
    Service --> Runtime
    Runtime --> Forsetti
```

**Single-binary product.** All three executables (service host, shell, bootstrapper) statically link `MasterControlApp.lib` so they share one in-process runtime and one configuration model.

---

## 2. Request lifecycle

Every incoming admin API request runs through three gates before reaching its handler.

```mermaid
sequenceDiagram
    autonumber
    participant Caller as Caller<br/>(operator OR LAN client)
    participant MW as Middleware
    participant Roster as LAN Client Roster
    participant Gate as Privilege Gate
    participant CLU as CLU enforceAction
    participant Handler as Route Handler
    participant Ring as Activity Ring

    Caller->>MW: HTTP request<br/>(X-MCOS-Client-Id optional)
    MW->>Roster: Resolve header
    alt Header missing or unknown
        Roster-->>MW: nullopt → operator-fallback context<br/>(all privileges, autonomous)
    else Client enabled
        Roster-->>MW: LanClient record<br/>(privileges, autonomousMode)
        MW->>Roster: touchClient(lastSeenUtc)
    else Client DISABLED
        Roster-->>MW: client.enabled == false
        MW-->>Caller: 403 "LAN client is disabled"
    end

    MW->>Gate: requirePrivilege(flag)
    alt Privilege missing
        Gate-->>Caller: 403 + privilege name
    else Privilege held
        Gate->>CLU: enforceAction(kind, target, actor)
        alt Allow
            CLU-->>Handler: proceed
            Handler->>Ring: emit activity event
            Handler-->>Caller: 200 / 204
        else Block
            CLU-->>Caller: 403 + ruleId + posture
        else RequiresOperatorApproval
            CLU-->>Caller: 202 + deferredActionId<br/>(staged in queue)
        end
    end
```

Reads on `/api/client/*` shared-fabric routes skip steps 5–10 — use is universal.

---

## 3. Forsetti module catalog

Default activation list lives at `src/MasterControlApp/MasterControlRuntime.cpp::activateDefaultModules` and registers exactly **16 modules**.

| # | Module id | Class | Role |
| --- | --- | --- | --- |
| 1 | `com.mastercontrol.environment-discovery` | `EnvironmentDiscoveryModule` | Detect host name, OS, primary bind address |
| 2 | `com.mastercontrol.host-telemetry` | `HostTelemetryModule` | CPU / memory / disk metrics |
| 3 | `com.mastercontrol.runtime-inventory` 🛡️ | `RuntimeInventoryModule` | MCP + sub-agent endpoint catalog |
| 4 | `com.mastercontrol.configuration` 🛡️ | `ConfigurationModule` | `AppConfiguration` persistence |
| 5 | `com.mastercontrol.installer-import` | `InstallerImportModule` | Package / repo / zip onboarding |
| 6 | `com.mastercontrol.export` | `ExportModule` | Server-authored artifacts including LAN client bundles |
| 7 | `com.mastercontrol.lan-client-access` ⭐ | `LanClientAccessModule` | LAN client roster + privileges + autonomous-mode |
| 8 | `com.mastercontrol.command-logic-unit` 🛡️ | `CommandLogicUnitModule` | CLU governance + approval queue |
| 9 | `com.mastercontrol.gateway-windows` | `WindowsGatewayModule` | Windows platform gateway lane |
| 10 | `com.mastercontrol.gateway-macos` | `MacGatewayModule` | macOS platform gateway lane |
| 11 | `com.mastercontrol.gateway-ios` | `IOSGatewayModule` | iOS platform gateway lane |
| 12 | `com.mastercontrol.governance-windows` | `WindowsGovernanceMcpServerModule` | Windows governance MCP server |
| 13 | `com.mastercontrol.governance-macos` | `MacGovernanceMcpServerModule` | macOS governance MCP server |
| 14 | `com.mastercontrol.governance-ios` | `IOSGovernanceMcpServerModule` | iOS governance MCP server |
| 15 | `com.mastercontrol.beacon-gateway` | `BeaconGatewayModule` | LAN UDP advertisement |
| 16 | `com.mastercontrol.dashboard-ui` 🛡️ | `DashboardUIModule` | Browser admin surface |

**Legend:**

- 🛡️ — protected. Cannot be disabled at runtime via `/api/forsetti/modules/state`.
- ⭐ — added in `v0.5.0` as part of the LAN client control plane rebuild.

---

## 4. Service container

`MasterControlApplication::Impl` constructs a single Forsetti `ServiceContainer` and registers the runtime services consumed across the codebase.

```mermaid
classDiagram
    class IConfigurationService {<<interface>>}
    class IRuntimeInventoryService {<<interface>>}
    class IMcpServerCatalogService {<<interface>>}
    class ISubAgentCatalogService {<<interface>>}
    class ISubAgentGroupService {<<interface>>}
    class ILanClientAccessService {<<interface>>}
    class IGovernanceApprovalQueueService {<<interface>>}
    class ICommandLogicUnitService {<<interface>>}
    class IExportService {<<interface>>}
    class IInstallerOrchestrator {<<interface>>}
    class IPlatformServiceCatalogService {<<interface>>}
    class IPlatformGovernanceToolService {<<interface>>}
    class IAppleRemoteHostService {<<interface>>}
    class IBeaconService {<<interface>>}
    class ITelemetryService {<<interface>>}
    class IAdminApiService {<<interface>>}
    class IModuleControlSurfaceService {<<interface>>}
    class IForsettiSurfaceService {<<interface>>}
    class IPackageTrustEvaluator {<<interface>>}

    IAdminApiService --> IMcpServerCatalogService
    IAdminApiService --> ISubAgentCatalogService
    IAdminApiService --> ISubAgentGroupService
    IAdminApiService --> IExportService
    IAdminApiService --> ICommandLogicUnitService
    IAdminApiService --> IAppleRemoteHostService
    IAdminApiService --> IInstallerOrchestrator
    IAdminApiService --> IPlatformServiceCatalogService
    IAdminApiService --> IForsettiSurfaceService
    IAdminApiService --> ITelemetryService
    IAdminApiService --> IRuntimeInventoryService
    IAdminApiService --> IConfigurationService

    IExportService --> ILanClientAccessService : per-client bundles
    ICommandLogicUnitService --> IConfigurationService : posture
    ICommandLogicUnitService --> IInstallerOrchestrator : trust history
    ICommandLogicUnitService --> IExportService : export visibility
```

Adding a new service is one line in `Impl::initialize` (`std::make_shared<...>`) and one line in `createForsettiRuntime` (`services->registerService<I...>(...)`).

---

## 5. The LAN Client Access module

`LanClientAccessModule` is the load-bearing addition of the rebuild.

```mermaid
flowchart LR
    classDef accent fill:#031018,stroke:#00F6FF,color:#E6FCFF;
    classDef good fill:#031a14,stroke:#1cf2c1,color:#a8efe0;

    Routes[/api/clients/* routes]:::accent
    Service[LanClientAccessService]:::accent
    Roster[(LanClient roster<br/>persisted in AppConfiguration)]:::good
    Activity[Activity Ring]:::accent
    Bundle[GET /api/clients/{id}/config]:::good

    Routes --> Service
    Service -- "upsert / disable / enable / remove" --> Roster
    Service -- "lifecycle events" --> Activity
    Bundle --> Service
    Service -. "snapshot" .-> Bundle
```

**Header file:** [`include/MasterControl/LanClient.h`](../../include/MasterControl/LanClient.h)
**Service interface:** [`include/MasterControl/ILanClientAccessService.h`](../../include/MasterControl/ILanClientAccessService.h)
**Implementation:** `class LanClientAccessService` in `src/MasterControlApp/MasterControlRuntime.cpp`
**Manifest:** [`src/MasterControlModules/Resources/ForsettiManifests/LanClientAccessModule.json`](../../src/MasterControlModules/Resources/ForsettiManifests/LanClientAccessModule.json)

The module itself is small — it announces the service to the framework and emits lifecycle events. The interesting code is the service implementation, which mirrors the catalog pattern used by `McpServerCatalogService` and `SubAgentCatalogService`.

---

## 6. CLU enforcement pipeline

CLU runs after the privilege gate has already cleared. Its job is to decide whether a privileged-and-allowed mutation actually proceeds, blocks on posture, or queues for operator approval.

```mermaid
flowchart TB
    classDef accent fill:#031018,stroke:#00F6FF,color:#E6FCFF;
    classDef block fill:#1f0a0c,stroke:#FF6A80,color:#ffd0d4;
    classDef defer fill:#1f1a08,stroke:#FFC857,color:#ffe0a0;
    classDef allow fill:#031a14,stroke:#1cf2c1,color:#a8efe0;

    Start([enforceAction<br/>action, target, actor]):::accent
    Posture{Snapshot.posture<br/>== blocked?}:::accent
    Resource{Resource envelope<br/>preflight failed?}:::accent
    Switch{action kind?}:::accent

    Block([Block + ruleId + findings]):::block

    CatalogMutation([Allow]):::allow
    ClientRoster([Allow]):::allow
    ModuleLifecycle([Allow]):::allow

    AutonomyEnable{request.source<br/>== "enable"?}:::accent
    GlobalAutonomy{aiAutonomyEnabled<br/>== true?}:::accent
    AutonomyAllow([Allow]):::allow
    AutonomyBlock([Block CLU-C009]):::block

    PolicyChange([Defer<br/>RequiresOperatorApproval]):::defer

    Start --> Posture
    Posture -- yes --> Block
    Posture -- no --> Switch

    Switch -- "GovernancePolicyChange" --> PolicyChange
    Switch -- "Mcp/SubAgent Create/Modify/Remove" --> CatalogMutation
    Switch -- "Client Register/Privilege/Revoke" --> ClientRoster
    Switch -- "Module Enable/Disable" --> ModuleLifecycle
    Switch -- "RemoteInstall" --> Resource
    Switch -- "AutonomousModeChange" --> AutonomyEnable

    AutonomyEnable -- no/disable --> AutonomyAllow
    AutonomyEnable -- yes/enable --> GlobalAutonomy
    GlobalAutonomy -- yes --> AutonomyAllow
    GlobalAutonomy -- no --> AutonomyBlock

    Resource -- yes --> Block
    Resource -- no --> CatalogMutation
```

A `RequiresOperatorApproval` outcome stages the original request body in `IGovernanceApprovalQueueService` so an operator can approve or reject without re-supplying the payload.

See [Governance](Governance) for the full action enum and rule catalog.

---

## 7. Operator surfaces

### Browser admin UI (`resources/web/`)

Single-page app, vanilla JS, no framework dependency. Six destinations:

```mermaid
flowchart LR
    classDef nav fill:#031018,stroke:#00F6FF,color:#E6FCFF;

    Hero["Hero<br/>(Register LAN Client • Open Clients • Approvals)"]:::nav

    Hero --> Overview[Overview<br/>posture · clients · approvals · telemetry]:::nav
    Hero --> Clients[LAN Clients<br/>table + drawer + bundle download]:::nav
    Hero --> Governance[Governance<br/>posture · approvals · decisions · rules]:::nav
    Hero --> Runtime[Shared Fabric<br/>MCP servers + sub-agents]:::nav
    Hero --> Activity[Activity<br/>full event ring viewer]:::nav
    Hero --> Exports[Exports<br/>artifact downloads]:::nav
```

Each destination polls the live admin API every 5 seconds. State is held in a single `state` object; rendering is full-redraw per destination on each poll.

### WinUI 3 desktop shell (`src/MasterControlShell/`)

Currently in deferred-cleanup state from the Phase 2b architecture rebuild. The browser admin UI delivers everything Phase 8 needed; the shell rebuild is queued as a separate post-`v0.5.0` track.

---

## 8. Activity ring

Every privileged mutation, governance decision, and lifecycle change emits an event into a 512-entry FIFO ring (`ActivityEventRing` at the bottom of `MasterControlRuntime.cpp`).

```
                    nextSequence_  ↓
   ┌──────┬──────┬──────┬──────┬──────┬──────┬──────┐
   │ ev 1 │ ev 2 │ ev 3 │ ev 4 │ ev 5 │ ev 6 │ ev 7 │  ← FIFO
   └──────┴──────┴──────┴──────┴──────┴──────┴──────┘
           ↑ pop_front when capacity (512) reached
```

Consumers poll `/api/activity?since={highWaterMarkId}` to stream incremental updates without reading the whole ring.

Event kinds keyed to the LAN client model:

- `lan-client-{created,updated,disabled,enabled,removed}`
- `lan-client-privileges-changed`
- `lan-client-autonomous-mode-changed`
- `governance-{deferred,approved,rejected}`
- `admin_api_request` (auto-captured at the dispatch layer)

See [Telemetry & Activity](Telemetry-and-Activity) for the full event-shape reference.

---

## 9. Configuration shape

`AppConfiguration` (declared in [`include/MasterControl/MasterControlModels.h`](../../include/MasterControl/MasterControlModels.h)) persists to `<dataDir>/config/master-control-orchestration-server.json`.

```mermaid
classDiagram
    class AppConfiguration {
        +string instanceName
        +string bindAddress
        +uint16_t browserPort
        +uint16_t beaconPort
        +bool aiAutonomyEnabled
        +bool advancedMode
        +SecuritySettings security
        +ResourceAllocationProfile resourceAllocation
        +vector~SubAgentGroupDefinition~ subAgentGroups
        +vector~LanClient~ lanClients
        +vector~AppleRemoteHost~ appleRemoteHosts
        +ManagedNodeProfile activeProfile
    }
    class LanClient {
        +string clientId
        +string displayName
        +string clientType
        +string hostName
        +string networkAddress
        +bool enabled
        +LanClientPrivileges privileges
        +bool autonomousMode
        +string createdAtUtc
        +string lastSeenUtc
        +string disabledAtUtc
    }
    class LanClientPrivileges {
        +bool canCreateMcpServers
        +bool canModifyMcpServers
        +bool canRemoveMcpServers
        +bool canCreateSubAgents
        +bool canModifySubAgents
        +bool canRemoveSubAgents
        +bool canManageClients
        +bool canManageModules
        +bool canChangeGovernancePolicy
    }
    AppConfiguration *-- LanClient : "lanClients[]"
    LanClient *-- LanClientPrivileges
```

---

## 10. Build composition

```
master-control-dashboard.sln
├── MasterControlApp.lib              ← shared runtime
│   ├── MasterControlRuntime.cpp     (≈9k LOC core)
│   ├── MasterControlDefaults.cpp
│   ├── MasterControlModels.cpp
│   ├── MasterControlDiagnostics.cpp
│   └── LanClientAccessService.*  (inlined inside MasterControlRuntime.cpp)
├── MasterControlServiceHost.exe      ← Windows service entry point
├── MasterControlShell.exe            ← WinUI 3 desktop (deferred)
├── MasterControlBootstrapper.exe     ← installer / repair lifecycle
└── MasterControlOrchestrationServerTests.exe   ← native test suite
```

Top-level `CMakeLists.txt` composes these from `vcpkg.json` dependencies. Build presets in `CMakePresets.json`.

---

## 11. Persistence paths

| Path | Contents |
| --- | --- |
| `%ProgramData%\Master Control Orchestration Server\config\master-control-orchestration-server.json` | `AppConfiguration` (instance + LAN clients + sub-agent groups + Apple hosts + active profile) |
| `%ProgramData%\Master Control Orchestration Server\state\install-history.json` | Install / import provenance |
| `%ProgramData%\Master Control Orchestration Server\state\apple-operations.json` | Apple operation queue + history |
| `%ProgramData%\Master Control Orchestration Server\state\entitlements.json` | Forsetti module unlock state |
| `<install-dir>\share\<version>\ForsettiManifests\*.json` | Module manifests |
| `<install-dir>\share\<version>\web\` | Browser admin UI assets |
| `<install-dir>\share\<version>\clu\governance-profile.json` | CLU profile (rules / roles / doctrine) |

Activity ring and approval queue are **process-memory only** — they reset on service restart.

---

## 12. What's NOT in the architecture

These are deliberately absent. Each one represents a locked decision in [ADR-001](Architecture-Decisions/ADR-001-lan-client-control-plane).

| Absent | Why |
| --- | --- |
| Bearer tokens / OAuth | The LAN is trusted; identity is by header. |
| TLS / certificates | Same. Re-introducing requires `httpsys` binding + cert lifecycle. |
| Outbound AI calls (CLI invocations) | MCOS is a server, not a client. AI agents call MCOS, never the other way. |
| Per-resource visibility / ACLs on the catalog | The shared fabric rule (CLU-S001) makes use universal. |
| Provider auto-connect / sign-in flows | Removed in Phase 2 of the rebuild. |

---

## See also

- [LAN Clients](LAN-Clients) — the user-facing identity model
- [Privileges](Privileges) — what each flag gates
- [Client Config Bundle](Client-Config-Bundle) — onboarding payload
- [Governance](Governance) — CLU rules and the approval queue
- [API Reference](API-Reference) — every HTTP route
- [ADR-001](Architecture-Decisions/ADR-001-lan-client-control-plane) — the architectural decision
