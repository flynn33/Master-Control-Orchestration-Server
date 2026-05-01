# Architecture Drift Inventory (PHASE-00 Baseline)

This file maps where the current MCOS implementation conflicts with the gateway-first model declared in [ADR-002](../wiki/Architecture-Decisions/ADR-002-gateway-first-mcp-realignment.md). It is a forward-looking inventory: each row identifies an existing surface and the phase that resolves it. PHASE-00 makes no edits; this is the map subsequent phases consume.

Snapshot date: 2026-04-30
Working tree: `master-control-dashboard-main`, post-overlay-install commit `1c5d986`.

## How to read this

| Column | Meaning |
|---|---|
| Surface | The current code, route, manifest, or doc artifact. |
| Today | What it currently does or contains. |
| Realignment target | What ADR-002 requires it to become or how it must change. |
| Resolves in | The phase in `handoff/realignment/manifest.json` that converts it. |
| Action | `keep` (no change), `extend` (add to it), `subsume` (replace with new endpoint and deprecate), `replace` (delete and rebuild), `split` (operator vs AI-client paths). |

---

## A. LAN discovery surface

| Surface | Today | Realignment target | Resolves in | Action |
|---|---|---|---|---|
| `BeaconService` in `src/MasterControlApp/MasterControlRuntime.cpp` lines 7133–7210 | UDP broadcast of `BeaconAdvertisement{instanceName, hostName, IP, browserPort, browserPort, "online", platformGateways}` on `configuration.beaconPort` every `beaconBroadcastIntervalSeconds`. | UDP beacon stays as fallback but its payload must align with `/.well-known/mcos.json` (gateway-first fields). Add DNS-SD/mDNS via Win32 `DnsServiceRegister` for `_mcos._tcp.local`, `_mcos-mcp._tcp.local`, `_mcos-onboarding._tcp.local` with TXT fields per `MCP-GATEWAY-DISCOVERY-CONTRACT.md`. | PHASE-03 | extend |
| `BeaconAdvertisement` model | Carries `instanceName`, `hostName`, `IP`, `browserPort` (twice — appears to double as both browser/admin and overall service port), `status`, `platformGateways`. | Carries gateway URL, governance bundle URL, onboarding URLs, capabilities array, `auth=none`/`trust=lan`. Schema: `docs/implementation/schemas/discovery-document.schema.json`. | PHASE-03 | extend |
| `GET /api/beacon` at `MasterControlRuntime.cpp:8504` | Returns the same `BeaconAdvertisement` JSON. | Add new `/.well-known/mcos.json` and `/api/discovery` endpoints with the gateway-first document; `/api/beacon` may stay as backward-compatibility alias. | PHASE-03 | extend |
| `BeaconGatewayModule` Forsetti manifest at `src/MasterControlModules/Resources/ForsettiManifests/BeaconGatewayModule.json` | Module advertises `networking`, `telemetry`, `event_publishing` capabilities; entry point `BeaconGatewayModule`. | Same module remains the natural home for both the UDP beacon and the new DNS-SD registration; capabilities may need to add `discovery` or `mcp_gateway` (manifest update only when justified, per Forsetti rule). | PHASE-03 | extend |
| `platformServiceCatalogService_->listGateways()` feeding `BeaconAdvertisement.platformGateways` | Lists platform services (Windows/macOS/iOS gateways for the platform-services config bundle) — predates the MCP Gateway concept. | Conceptually adjacent but distinct from the new `gateway: {type, mcpUrl, healthUrl}` field. Both can coexist; the new MCP gateway is the `gateway.*` block, the existing platform gateways become `platformGateways[]` and may be renamed to avoid confusion. | PHASE-03 | rename or split |

## B. MCP Gateway surface (PHASE-02 resolution)

| Surface | Today | Realignment target | Resolves in | Action |
|---|---|---|---|---|
| MCP Gateway adapter | **Landed PHASE-02.** `IMcpGateway` lives at `include/MasterControl/MasterControlContracts.h`. `McpJungleGatewayAdapter` (production) and `FakeMcpGatewayAdapter` (tests) live at `include/MasterControl/McpGatewayAdapters.h` and `src/MasterControlApp/McpGatewayAdapters.cpp`. Adapter is replaceable behind the interface. | Replaced — `Start/Stop/CurrentStatus/Probe/RegisterHttpServer/RegisterStdioServer/DeregisterServer/ListTools/GatewayMcpUrl/AdapterType` all implemented. | PHASE-02 | done |
| Gateway configuration | **Landed PHASE-02.** `McpGatewayConfiguration` in `MasterControlModels.h` carries `type/enabled/binaryPath/listenHost/listenPort/mcpPath/healthPath/databasePath/mode` matching `gateway-service.schema.json`. Defaulted via `buildDefaultConfiguration()` (disabled, port 8080, lan-trusted). | Replaced — `AppConfiguration::mcpGateway` round-trips JSON. | PHASE-02 | done |
| Gateway HTTP routes | **Landed PHASE-02.** `GET /api/gateway/{status,health,tools}` and `POST /api/gateway/{start,stop}` served by the runtime. Surfaced into `DashboardSnapshot.{mcpGatewayStatus,mcpGatewayHealth,mcpGatewayTools}`. | Surface gateway health through the runtime API; populate the dashboard's gateway panel. | PHASE-02 done; PHASE-09 wires UI panel. | done (API), pending (UI) |
| Logical pool registration with gateway | **Landed PHASE-02.** Runtime registers one logical MCP server (`mcos-default-pool`) with the adapter at boot. PHASE-06 will replace this single registration with per-pool registrations once `ManagedEndpointPool` lands. | Each pool registers exactly one logical server; autoscaled instances are NOT registered separately. | PHASE-02 (path), PHASE-06 (pools) | path done, pools pending |

## C. Onboarding profile surface

| Surface | Today | Realignment target | Resolves in | Action |
|---|---|---|---|---|
| `GET /api/platform-services/config/{platform}` at `MasterControlRuntime.cpp:8985–8987`, helper at `:1391` | Server-generated configuration bundle keyed by platform (`windows`, `macos`, `ios`). Already proves the "server emits client config" pattern. | Subsumed by `/api/onboarding/{clientType}` keyed on the AI client (`claude-code`, `codex`, `grok`, `chatgpt`, `generic`). Each profile points at one MCP gateway URL, declares `authRequired=false`, links the governance bundle. Schema: `onboarding-profile.schema.json`. | PHASE-04 | subsume |
| `/api/clients/{id}/config` at `MasterControlRuntime.cpp:3532` (per ADR-001) | Per-LanClient bundle download for X-MCOS-Client-Id-style integration. | Stays on the operator surface for clients that opt into the per-client privilege model; no longer the AI-client onboarding path. | PHASE-04 | split |
| Browser onboarding UI (`resources/web/app.js`) | Lan-clients flow with X-MCOS-Client-Id assumptions. | Shows per-client-type onboarding cards backed by `/api/onboarding/*`; preserves manual/import paths as first-class options. | PHASE-04, PHASE-09 | extend |
| Companion utility | None. | Optional small utility documented in PHASE-04 that browses DNS-SD, fetches `/api/onboarding/{clientType}`, writes/prints the client config, verifies connectivity. May be deferred. | PHASE-04 | replace (new) |

## D. Governance bundle surface

| Surface | Today | Realignment target | Resolves in | Action |
|---|---|---|---|---|
| `resources/clu/governance-profile.json` | Single profile defining CLU doctrine, policies, action kinds. | Stays as the source of truth for CLU policy text; gets folded into platform-specific governance bundles served at `/api/governance/bundles/{platform}`. | PHASE-05 | extend |
| `GET /api/clu`, `GET /api/clu/tools`, `GET /api/clu/apple-operations` (`MasterControlRuntime.cpp:8486, 8489, 8492`) | Operator-facing CLU surface used by the browser dashboard. | Stays on the operator surface. | PHASE-05 | keep |
| `GET /api/client/governance/profile`, `POST /api/client/governance/decisions` (`MasterControlRuntime.cpp:8552, 8568`) | Per-LanClient governance read/decide path with X-MCOS-Client-Id. | Stays on the operator-side per-client surface. The new AI-client governance path is the bundle URL embedded in the onboarding profile, not a per-request governance check. | PHASE-05 | split |
| `/api/governance/bundles/{windows|macos|ios}`, `/api/governance/profile`, `/api/governance/decisions` | Not present. | New endpoints serving bundles with `platform`, `forsettiFrameworkVersion`, `agenticCodingFrameworkVersion`, `cluSchemaVersion`, `instructionsMarkdown`, `rulesJson`, `decisionPolicy`, `checksum`, `generatedAt`. Contract: `CLU-GOVERNANCE-BUNDLE-CONTRACT.md`. | PHASE-05 | replace (new) |
| `GovernanceActionKind` enum | Expanded to 15 action kinds in ADR-001. | Stays. PHASE-05 may add bundle-distribution-related decision points but does not retract existing kinds. | PHASE-05 | keep |
| `scripts/check-mastercontrol-forsetti.ps1` | Forsetti compliance script. Currently asserts the post-ADR-001 module shape. | Updated when architecture changes invalidate its assertions; the gateway-first changes likely require new assertions for gateway/pool/discovery modules. | PHASE-05 (or PHASE-10) | extend |

## E. Worker pool / supervision / autoscaling surface

| Surface | Today | Realignment target | Resolves in | Action |
|---|---|---|---|---|
| Managed endpoint pool model | Not present. Sub-agents and MCP servers exist as catalog entries (per ADR-001) but not as supervised process pools. | Introduce `EndpointTemplate`, `EndpointInstance`, `ManagedEndpointPool`, `WorkerSupervisor`, `HealthProbe`, `WorkerTelemetry`. Lifecycle: `configured/starting/ready/busy/draining/failed/stopped`. Schema: `managed-endpoint-pool.schema.json`. | PHASE-06 | replace (new) |
| Worker process supervision | Sub-agent fleet is documented as out-of-repo at `D:\Sub-Agents\` per ADR-001; no in-repo supervisor. | Worker process trees contained in Windows Job Objects; deterministic terminate/drain on shutdown. | PHASE-06 | replace (new) |
| Lease routing | Not present. | `EndpointLease`, `LeaseRouter` assigning sessions to instances behind stable logical pool endpoints. Sticky for stateful sessions. | PHASE-07 | replace (new) |
| Autoscaling | Not present. | `ScalePolicy` / `DrainPolicy` with thresholds (`minInstances`, `maxInstances`, `maxActiveLeasesPerInstance`, `scaleOutQueueWaitMs`, `scaleInIdleSeconds`). New leases route to new instances; active sessions drain. | PHASE-07 | replace (new) |
| `/api/runtime/mcp-servers`, `/api/runtime/subagents`, `/api/runtime/subagent-groups` (and `/remove` siblings) at `MasterControlRuntime.cpp:9073–9210` | Operator-facing CRUD over the catalog with privilege checks. | Stays on the operator surface as the way operators register backends; pool runtime state is added alongside (utilization, lease counts, etc). | PHASE-06, PHASE-07 | extend |

## F. Telemetry surface

| Surface | Today | Realignment target | Resolves in | Action |
|---|---|---|---|---|
| `ITelemetryService` (single host snapshot per `BeaconService` use at `MasterControlRuntime.cpp:7144–7148`) | Captures hostName, primary IP; powers beacon advertisement and dashboard. | Split into host telemetry (PDH for CPU/disk/network/process; DXGI for GPU memory), client telemetry (heartbeat-supplied only), gateway telemetry, worker telemetry, and an activity event taxonomy. Schema: `telemetry-event.schema.json`. | PHASE-08 | extend |
| Per-AI-client CPU/GPU/disk metrics | Not modeled. | Only when supplied by client heartbeat or sidecar; otherwise honest "unavailable" state. **No fake utilization.** | PHASE-08 | replace (new) |
| Activity event kinds | Existing operator activity ring covers connection/heartbeat/governance per ADR-001. | Extend to gateway lifecycle, worker lifecycle, scale events, governance events, discovery events. | PHASE-08 | extend |

## G. Tron dashboard / browser admin surface

| Surface | Today | Realignment target | Resolves in | Action |
|---|---|---|---|---|
| `resources/web/app.js`, `index.html`, `styles.css` | Six destinations centered on Overview, LAN Clients, Governance, Shared Fabric, Activity, Exports (per `README.md`). 32 files reference `X-MCOS-Client-Id`/`LanClient` directly or via routes. | New panels for: gateway status (MCPJungle health), client/model roster, host telemetry meters, worker pool gauges with utilization, lease/router/autoscale state, governance bundle downloads, real-time activity log, onboarding/setup view. Manual and import setup paths preserved. | PHASE-09 | extend & rebrand |
| Provider-era browser sign-in cards | Already removed (per ADR-001 §1, evidenced by zero `/api/providers` route in runtime). | Remain absent; ADR-002 forbids reintroduction. | — | n/a |
| Browser auth | None — trusted LAN per ADR-001 §3. | Operator surface stays as trusted LAN; AI-client surface moves to a logically distinct gateway port with `auth=none`/`trust=lan`. | PHASE-09, PHASE-10 | split |
| WinUI shell (`src/MasterControlShell/`) | Deferred-cleanup state with residual `ProvidersSectionControl` references that don't compile (see `PROVIDER-ERA-REMOVAL-MAP.md`). | Either delete the shell entirely or rebuild it around the new model in a follow-on track; ADR-001 already accepted browser-only operator surface during the rebuild. | PHASE-01 (cleanup) and beyond | replace |

## H. Trust / authentication boundary

| Surface | Today | Realignment target | Resolves in | Action |
|---|---|---|---|---|
| Operator surface (`/api/clients/*`, `/api/runtime/*`, `/api/forsetti/*`, `/api/clu/approvals`, `/api/setup/*`, `/api/exports`, `/api/install/*`) | LAN-trusted; `X-MCOS-Client-Id` middleware identifies actor for activity attribution; per-client privilege flags gate mutations. | **Unchanged**. ADR-002 §1 keeps this model on the operator surface. | — | keep |
| AI-client surface (today: `GET /api/client/mcp-servers`, `/api/client/sub-agents`, `/api/client/activity`, `/api/client/governance/profile`, `POST /api/client/governance/decisions`, `/api/client/heartbeat` at `MasterControlRuntime.cpp:8519–8598`) | Per-LanClient with required `X-MCOS-Client-Id`. | The new AI-client gateway URL replaces this as the primary tool path. The existing routes can stay as a per-client read-surface for clients that opt into the operator-style integration, but they are no longer the documented AI-client onboarding target. | PHASE-04, PHASE-09 | split |
| MCP Gateway port (logically separate from admin) | Not present. | New port for the MCP Gateway URL with `auth=none`. Windows Firewall scoping and origin/host validation enforce trust at the network/transport level. | PHASE-02, PHASE-10 | replace (new) |

## I. CI / release / packaging

| Surface | Today | Realignment target | Resolves in | Action |
|---|---|---|---|---|
| `.github/workflows/` Windows build/test/package gate | Existing per README ("Windows product gate"). Toolchain discovery details and bypass risk not yet audited. | CI gate blocks release; no `workflow_dispatch` bypass; `vswhere`/preset-driven toolchain discovery; no hardcoded VS Enterprise path; version stamped before configure/build/package. | PHASE-10 | extend |
| `VERSION.json` | Source of truth; current `0.5.0`. | No bump until PHASE-10 explicitly requires it. | PHASE-10 | keep |
| `installer/` | Existing installer payload. | Document gateway binary/dependency packaging; bootstrapper smoke check. | PHASE-10 | extend |
| Firewall / LAN mode docs | Implicit; not consolidated. | Documented in onboarding/operations. | PHASE-10 | extend |

## J. Native gateway evaluation

| Surface | Today | Realignment target | Resolves in | Action |
|---|---|---|---|---|
| MCPJungle external dependency | Not yet integrated. PHASE-02 introduces it as a supervised child process behind the C++ adapter. | Operational evaluation: keep MCPJungle, or build a native gateway on HTTP.sys/WinHTTP. Decision documented; if native is selected, new phases are proposed but not implemented in PHASE-11. | PHASE-11 | decide |

---

## Cross-cutting non-drifts (already aligned with ADR-002)

These exist already and need no realignment:

- ADR-001 provider-removal stance (reaffirmed by ADR-002 §1).
- `Forsetti-Framework-Windows-main/` vendoring (untouchable; ADR-002 §11).
- LAN trust as the security model.
- The browser-as-primary-operator-surface decision (ADR-001 negative-consequences acknowledgement).
- Hand-authored CHANGELOG and version policy.
- The "no AI contributor attribution" guard in `.github/`.

## Open questions surfaced by this inventory

These are noted here for visibility; resolution is owned by the phase listed.

1. **Browser/admin port vs MCP gateway port** — whether they live on the same host:port with different paths, or distinct ports. PHASE-02 should fix this. The schema's default `listenPort=8080` for the gateway suggests distinct from the existing admin port `7300`.
2. **Beacon overlap with platform gateways** — the existing `BeaconAdvertisement.platformGateways` list (platform-services config gateways) is conceptually distinct from the new MCP gateway. Renaming or namespacing in PHASE-03 will prevent confusion.
3. **Companion utility scope** — whether MCOS ships a small EXE that auto-applies onboarding for clients that cannot consume DNS-SD, or whether documentation suffices. PHASE-04 decides.
4. **Shell future** — ADR-001 deferred. PHASE-01 may delete the shell's residual provider code; PHASE-09 reskin is browser-focused; the shell's eventual rebuild remains an open track.
5. **CLU per-client vs platform bundle endpoints** — both exist after PHASE-05. Documentation must be unambiguous about which is which.
