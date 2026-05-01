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

## A. LAN discovery surface (PHASE-03 resolution)

| Surface | Today | Realignment target | Resolves in | Action |
|---|---|---|---|---|
| `BeaconService` in `src/MasterControlApp/MasterControlRuntime.cpp` | **Landed PHASE-03.** UDP broadcast payload now serializes the gateway-first `DiscoveryDocument` (composed by `DiscoveryService`) instead of the legacy `BeaconAdvertisement`. Broadcast cadence and port unchanged. | Replaced — beacon is gateway-first. | PHASE-03 | done |
| `BeaconAdvertisement` model | **Retained** for `/api/beacon` backward-compatibility. Browser dashboards that already call `/api/beacon` continue to work. New consumers use `DiscoveryDocument` via `/api/discovery` or `/.well-known/mcos.json`. | Coexists — the new `DiscoveryDocument` carries gateway URL / governance / onboarding / capabilities / `auth=none` / `trust=lan` per the schema; the legacy struct keeps its `platformGateways` Forsetti context. | PHASE-03 | split (keep legacy + add new) |
| `GET /.well-known/mcos.json`, `GET /api/discovery` | **Landed PHASE-03.** Both routes serve the new `DiscoveryDocument`. `/.well-known/mcos.json` strips beacon-only fields (`generatedAtUtc`, `serverIpAddress`, `instanceName`) so it's strictly schema-conformant. `/api/discovery` returns the full document including beacon metadata. | Both routes wired through `IDiscoveryService::currentDocument()`. | PHASE-03 | done |
| DNS-SD service registration | **Landed PHASE-03.** `DiscoveryService::start()` registers `_mcos._tcp.local`, `_mcos-mcp._tcp.local`, `_mcos-onboarding._tcp.local` via Win32 `DnsServiceRegister` using the `PlatformServiceCatalogService` pattern. TXT fields (`product`/`role`/`gateway`/`mcp_path`/`config_path`/`governance_path`/`protovers`/`auth`/`trust`/`clu`/`forsetti`) match `MCP-GATEWAY-DISCOVERY-CONTRACT.md`. | All three service types advertised. | PHASE-03 | done |
| `BeaconGatewayModule` Forsetti manifest | Unchanged. The new `DiscoveryService` lives in the runtime translation unit; this module's role and capabilities remain valid for the broader beacon/networking surface. | Manifest update deferred — none of its assumptions are invalidated. | — | keep |
| `platformGateways[]` overlap | **Resolved.** The new `DiscoveryDocument.gateway` is a single object (type/mcpUrl/healthUrl/state), distinct from the legacy `BeaconAdvertisement.platformGateways[]` array of Forsetti platform-services descriptors. The two carry different meanings: the new field is the MCP Gateway URL; the legacy field is the platform-services Forsetti gateway list. No rename required. | Disambiguated by distinct field names and distinct service types. | PHASE-03 | done |
| `instanceId` persistence | **Landed PHASE-03.** `AppConfiguration::instanceId` field added; `buildDefaultConfiguration()` generates a UUID-backed identifier (`mcos-<uuid>`) via Win32 `UuidCreate`. Operators can override by editing `mcos.json`; the field round-trips through every config persistence path. | Generated. | PHASE-03 | done |

## B. MCP Gateway surface (PHASE-02 resolution)

| Surface | Today | Realignment target | Resolves in | Action |
|---|---|---|---|---|
| MCP Gateway adapter | **Landed PHASE-02.** `IMcpGateway` lives at `include/MasterControl/MasterControlContracts.h`. `McpJungleGatewayAdapter` (production) and `FakeMcpGatewayAdapter` (tests) live at `include/MasterControl/McpGatewayAdapters.h` and `src/MasterControlApp/McpGatewayAdapters.cpp`. Adapter is replaceable behind the interface. | Replaced — `Start/Stop/CurrentStatus/Probe/RegisterHttpServer/RegisterStdioServer/DeregisterServer/ListTools/GatewayMcpUrl/AdapterType` all implemented. | PHASE-02 | done |
| Gateway configuration | **Landed PHASE-02.** `McpGatewayConfiguration` in `MasterControlModels.h` carries `type/enabled/binaryPath/listenHost/listenPort/mcpPath/healthPath/databasePath/mode` matching `gateway-service.schema.json`. Defaulted via `buildDefaultConfiguration()` (disabled, port 8080, lan-trusted). | Replaced — `AppConfiguration::mcpGateway` round-trips JSON. | PHASE-02 | done |
| Gateway HTTP routes | **Landed PHASE-02.** `GET /api/gateway/{status,health,tools}` and `POST /api/gateway/{start,stop}` served by the runtime. Surfaced into `DashboardSnapshot.{mcpGatewayStatus,mcpGatewayHealth,mcpGatewayTools}`. | Surface gateway health through the runtime API; populate the dashboard's gateway panel. | PHASE-02 done; PHASE-09 wires UI panel. | done (API), pending (UI) |
| Logical pool registration with gateway | **Landed PHASE-02.** Runtime registers one logical MCP server (`mcos-default-pool`) with the adapter at boot. PHASE-06 will replace this single registration with per-pool registrations once `ManagedEndpointPool` lands. | Each pool registers exactly one logical server; autoscaled instances are NOT registered separately. | PHASE-02 (path), PHASE-06 (pools) | path done, pools pending |

## C. Onboarding profile surface (PHASE-04 resolution)

| Surface | Today | Realignment target | Resolves in | Action |
|---|---|---|---|---|
| `GET /api/onboarding`, `GET /api/onboarding/{clientType}` | **Landed PHASE-04.** The runtime serves a typed profile for `claude-code`, `codex`, `grok`, `chatgpt`, and falls through to `generic` for unknown client types. Each profile carries the live `gatewayMcpUrl` from the discovery document, `authRequired=false`, `trust=lan`, the governance bundle URL, the discovery URL, the instance id, and per-client config snippets / manual instructions / verification steps / caveats. | Replaced — schema-conformant per `onboarding-profile.schema.json`. | PHASE-04 | done |
| Existing `/api/platform-services/config/{platform}` | Unchanged. The platform-services endpoint stays for Forsetti platform-services callers; AI-client onboarding now flows through `/api/onboarding/{clientType}`. | Coexists; the new endpoint is the documented AI-client onboarding path. | PHASE-04 | split (keep legacy + add new) |
| `/api/clients/{id}/config` (ADR-001) | Unchanged. Per-LanClient operator-surface bundle download remains for the privilege-flag integration model. | Stays on operator surface; not the AI-client path. | — | keep |
| Browser onboarding UI (`resources/web/app.js`) | Existing operator dashboard; not yet pointed at `/api/onboarding/*`. | PHASE-09 (Tron dashboard realignment) wires the UI. Profiles are content-stable today and ready for that consumer. | PHASE-09 | pending |
| Companion utility for ChatGPT connector-edge | Profile documents the connector-edge constraint via `caveats`. The actual companion binary is not shipped in PHASE-04. | Documented in `caveats` and `manualInstructions`; the binary lands later if needed. | PHASE-04 docs done; binary deferred | deferred |

## D. Governance bundle surface (PHASE-05 resolution)

| Surface | Today | Realignment target | Resolves in | Action |
|---|---|---|---|---|
| `resources/clu/governance-profile.json` | Unchanged. Source of truth for CLU doctrine, policies, action kinds, roles, rules. `GovernanceBundleService` reads this on every bundle request so operator edits propagate without restart. | Source of truth honored. | PHASE-05 | done |
| `GET /api/clu`, `GET /api/clu/tools`, `GET /api/clu/apple-operations` | Unchanged. Operator-facing CLU surface preserved. | Stays on the operator surface. | — | keep |
| `GET /api/client/governance/profile`, `POST /api/client/governance/decisions` | Unchanged. Per-LanClient governance read/decide path remains for operator-flagged clients. | Stays. The new AI-client governance path is the bundle URL embedded in the onboarding profile (PHASE-04 + PHASE-05). | — | keep |
| `GET /api/governance/bundles/{windows|macos|ios}`, `GET /api/governance/profile`, `GET /api/governance/decisions`, `GET /api/governance/bundles` | **Landed PHASE-05.** All four routes wired through `IGovernanceBundleService`. Bundles carry `platform` / `forsettiFrameworkVersion` / `agenticCodingFrameworkVersion` / `cluSchemaVersion` / `instructionsMarkdown` / `rulesJson` / `decisionPolicy` / `checksum` (sha256) / `generatedAt`. `/api/governance/decisions` advertises the POST contract; the live POST handler lands in PHASE-06/07. | Replaced. | PHASE-05 | done |
| Vendored Forsetti instructions (`Forsetti-Framework-Windows-main/.../forsetti-instructions.json`) | **Read-only consumer.** `GovernanceBundleService::loadForsettiInstructions()` reads the file at request time to populate `forsettiFrameworkVersion`. Vendored content is not modified (per `.claude/rules/20-forsetti-clu-governance.md`). | Honored. | PHASE-05 | done (read-only) |
| `GovernanceActionKind` enum | Unchanged. 14 action kinds (post-PHASE-01 cleanup of provider-era kinds). | Stays. | — | keep |
| `scripts/check-mastercontrol-forsetti.ps1` | **Updated PHASE-05.** Six stale assertions about `resources/web/app.js`'s Forsetti-surface bootstrap (relics of pre-ADR-001 browser shape) retired. New assertions enforce: no provider-era sign-in cards, no provider-era API calls, no hardcoded CLU surface keys. Script now passes (`Master Control Forsetti checks passed.`). | Compliance gate green. | PHASE-05 | done |

## E. Worker pool / supervision / autoscaling surface (PHASE-06 resolution)

| Surface | Today | Realignment target | Resolves in | Action |
|---|---|---|---|---|
| Managed endpoint pool model | **Landed PHASE-06.** `EndpointTemplate` / `EndpointInstance` / `ManagedEndpointPool` / `ScalePolicy` / `DrainPolicy` / `HealthProbeSpec` / `WorkerTelemetry` types in `MasterControlModels.h` matching `managed-endpoint-pool.schema.json`. Full JSON round-trip with explicit `template_`/`template` JSON-key alias. | Replaced. | PHASE-06 | done |
| 7-state instance lifecycle | **Landed PHASE-06.** `EndpointInstanceState::{Configured,Starting,Ready,Busy,Draining,Failed,Stopped}` enum with full to_string / fromString / to_json / from_json. `testEndpointInstanceStateAllSevenLifecycleStates` pins each slug. | Replaced. | PHASE-06 | done |
| Worker process supervision | **Landed PHASE-06.** `WorkerSupervisor` class spawns children via `CreateProcessW` + `AssignProcessToJobObject` with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`; supervisor destructor + runtime `shutdown()` reap the tree atomically. Supervised-mock fallback when binary is missing (ADR-002 §9: no fake live infrastructure). | Replaced. | PHASE-06 | done |
| Pool admin surface | **Landed PHASE-06.** `GET /api/pools`, `GET /api/pools/{poolId}`, `POST /api/pools` (upsert), `POST /api/pools/{poolId}/{remove\|scale\|drain}` wired through `IWorkerSupervisor`. | Replaced. | PHASE-06 | done |
| Lease routing | Not present. | `EndpointLease`, `LeaseRouter` assigning sessions to instances behind stable logical pool endpoints. Sticky for stateful sessions. | PHASE-07 | pending |
| Autoscaling | Scale policy fields exist on the model; the policy is not yet enforced. | `ScalePolicy` / `DrainPolicy` thresholds enforced via the lease router; new leases route to new instances; active sessions drain. | PHASE-07 | pending |
| `/api/runtime/mcp-servers`, `/api/runtime/subagents`, `/api/runtime/subagent-groups` | Unchanged operator-facing catalog CRUD. | Stays on the operator surface as the way operators register backends; per-pool runtime state is added alongside via `/api/pools`. | PHASE-06 / PHASE-07 | extended |

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
