# Phase Completion Report — PHASE-03

Phase: PHASE-03 — Bonjour-style LAN discovery and beacon correction
Phase file: [handoff/realignment/PHASE-03-bonjour-lan-discovery.md](../../../handoff/realignment/PHASE-03-bonjour-lan-discovery.md)
Manifest: [handoff/realignment/manifest.json](../../../handoff/realignment/manifest.json)
Date: 2026-05-01
Working tree: `master-control-dashboard-main`
Pre-phase commit: `e51581b` (PHASE-02 completion report)
Phase commit: `6f37cf0` (feat(phase-03): Bonjour-style LAN discovery and beacon correction)

## Scope completed

PHASE-03 introduced the gateway-first discovery surface declared by ADR-002 §4. MCOS now advertises itself on the LAN with three DNS-SD service types (`_mcos._tcp.local`, `_mcos-mcp._tcp.local`, `_mcos-onboarding._tcp.local`) carrying TXT metadata that points at the MCP gateway URL, the onboarding endpoints, and the governance bundle base URL. A new `DiscoveryDocument` is the canonical shape for all of: `/.well-known/mcos.json`, `/api/discovery`, the UDP beacon payload, and the dashboard snapshot. The legacy `BeaconAdvertisement` is retained only on `/api/beacon` for browser-dashboard backward compatibility.

The new `IDiscoveryService` abstraction (in `MasterControlContracts.h`) is the only path the runtime, beacon, and admin API use to access the document — keeping discovery logic centralized and testable. The DNS-SD registration path borrows the existing `PlatformServiceCatalogService::registerGatewayLocked` pattern (IPv4/IPv6 detection, instance label composition, error capture) so PHASE-03 does not introduce a second mDNS implementation. Cleanup paths deregister all three records during `shutdown()` before service teardown.

`AppConfiguration::instanceId` is a new persisted field. `buildDefaultConfiguration()` generates a UUID-backed identifier (`mcos-<lowercased-uuid>`) via Win32 `UuidCreate` on first run; operators can override by editing `mcos.json`.

## Files changed

| File | Change summary |
|---|---|
| [include/MasterControl/MasterControlContracts.h](../../../include/MasterControl/MasterControlContracts.h) | Added `IDiscoveryService` interface (`currentDocument()` / `start()` / `stop()`) above `IMcpGateway`. |
| [include/MasterControl/MasterControlModels.h](../../../include/MasterControl/MasterControlModels.h) | New structs `DiscoveryGateway`, `DiscoveryOnboarding`, `DiscoveryGovernance`, `DiscoveryDocument` with full `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT` adapters. `AppConfiguration` grows `instanceId`; `DashboardSnapshot` grows `discovery`. |
| [src/MasterControlApp/MasterControlRuntime.cpp](../../../src/MasterControlApp/MasterControlRuntime.cpp) | New `DiscoveryService` class (anonymous namespace) composing the document and registering three DNS-SD services via `DnsServiceRegister`. `BeaconService` rewired to broadcast the `DiscoveryDocument` JSON. `AdminApiService` accepts an `IDiscoveryService` and surfaces it on the dashboard snapshot. New routes: `GET /.well-known/mcos.json` (schema-strict) and `GET /api/discovery` (full doc with beacon metadata). Lifecycle: `discoveryService_->start()` after gateway construction, `shutdown()` deregisters before teardown. |
| [src/MasterControlApp/MasterControlDefaults.cpp](../../../src/MasterControlApp/MasterControlDefaults.cpp) | New `generateInstanceIdUtf8()` helper using Win32 `UuidCreate` / `UuidToStringA` / `RpcStringFreeA`; lowercased with `mcos-` prefix. `buildDefaultConfiguration()` seeds `instanceId`. |
| [src/MasterControlApp/CMakeLists.txt](../../../src/MasterControlApp/CMakeLists.txt) | Adds `rpcrt4` to MasterControlApp PRIVATE link list. |
| [tests/MasterControlOrchestrationServerTests.cpp](../../../tests/MasterControlOrchestrationServerTests.cpp) | 4 new tests: `testDiscoveryDocumentDefaultShape`, `testDiscoveryDocumentJsonRoundTrip`, `testWellKnownDocumentMatchesSchemaRequiredFields`, `testInstanceIdGeneration`. |
| [docs/implementation/ARCHITECTURE-DRIFT-INVENTORY.md](../../implementation/ARCHITECTURE-DRIFT-INVENTORY.md) | Section A (LAN discovery surface) rows flipped from "extend"/"replace (new)" to "done" with implementation paths. |
| [docs/implementation/FORBIDDEN-CONTRACT-GREP-LIST.md](../../implementation/FORBIDDEN-CONTRACT-GREP-LIST.md) | New section 1.7a (discovery document `auth`/`trust` integrity) — regression detection for any attempt to weaken the const `auth=none` / `trust=lan` invariants on the AI-client surface. |

Total: 8 files changed, +595 / -14 lines.

## Public contracts changed

- **C++ headers** — new `IDiscoveryService` interface; new `DiscoveryDocument` and nested struct types; new `instanceId` field on `AppConfiguration`; new `discovery` field on `DashboardSnapshot`. JSON round-trip is stable; existing consumers ignore the new fields cleanly.
- **HTTP API** — added two new routes: `GET /.well-known/mcos.json` (strictly schema-conformant per `discovery-document.schema.json`) and `GET /api/discovery` (full document including beacon-only metadata). `/api/beacon` is unchanged for backward compatibility.
- **UDP beacon payload** — now broadcasts `DiscoveryDocument` JSON instead of legacy `BeaconAdvertisement`. Beacon port and broadcast cadence unchanged.
- **DNS-SD advertisement** — three new service types registered on the local link with the TXT field set documented in `MCP-GATEWAY-DISCOVERY-CONTRACT.md`.
- **Configuration** — `mcos.json` files written before PHASE-03 deserialize cleanly (the `_WITH_DEFAULT` macro fills `instanceId` empty; the discovery service falls back to `"mcos-unidentified"` if not regenerated). New configurations always carry an instance ID.

## Tests added or updated

4 new tests in `MasterControlOrchestrationServerTests.cpp`:

| Test | What it pins |
|---|---|
| `testDiscoveryDocumentDefaultShape` | Default-constructed doc carries `product=MCOS`, `role=mcp-gateway-host`, `trust=lan`, `auth=none`. |
| `testDiscoveryDocumentJsonRoundTrip` | All gateway/onboarding/governance/capabilities fields survive JSON serialization both ways. |
| `testWellKnownDocumentMatchesSchemaRequiredFields` | After stripping `generatedAtUtc`/`serverIpAddress`/`instanceName`, the required keys per `discovery-document.schema.json` (`product`, `role`, `instanceId`, `trust`, `auth`, `gateway`, `onboarding`, `governance`, `capabilities`) are all present and the beacon-only keys are absent. |
| `testInstanceIdGeneration` | `buildDefaultConfiguration()` produces a non-empty, `mcos-`-prefixed, unique-per-call identifier. Pins the UUID-backed approach. |

The pre-existing 26 tests (LanClient model, governance enums, deferred actions, MCP gateway adapter state machine and JSON shape) all continue to pass without modification.

## Validation performed

| Command | Result | Notes |
|---|---|---|
| `cmake --preset debug` (with `VCPKG_ROOT` set to VS-bundled vcpkg) | succeeded | `rpcrt4` added to link list cleanly. |
| `cmake --build --preset debug` | **succeeded** — 0 errors | Same pre-existing C4100 warning in `SetupWizardBuilder.cpp(133)` as PHASE-01/02; no new diagnostics. |
| `ctest --preset debug --output-on-failure` | **4/4 passed** in ~2.0s | 30 total test functions running inside `MasterControlOrchestrationServerTests` (4 new in this phase). |
| Static API route check | Confirmed by code review at `MasterControlRuntime.cpp` route block: `GET /.well-known/mcos.json`, `GET /api/discovery`, plus the existing `GET /api/beacon`, `GET /api/gateway/{status,health,tools}`, `POST /api/gateway/{start,stop}`. | The well-known route lives outside `/api/` and is wired before the activity-stream filter, so it is not accidentally routed through the activity stream interceptor. |
| Static DNS-SD code review | `DiscoveryService::registerOneLocked` mirrors `PlatformServiceCatalogService::registerGatewayLocked` (same IPv4/IPv6 fallback, same `DnsServiceConstructInstance` + `DnsServiceRegister` sequence, same error capture). DNS-SD service types match the contract verbatim. | No live `dns-sd -B _mcos._tcp local.` probe was run in this dev environment; deferred to PHASE-04 when an operator runs MCOS on a real LAN. |
| `git grep` discovery-doc auth/trust integrity (FORBIDDEN-CONTRACT §1.7a) | 0 matches | Confirms `auth=none` / `trust=lan` are the only assignments in the discovery composition path. |

## Acceptance criteria status (from manifest)

| Criterion | Status | Evidence |
|---|---|---|
| DNS-SD TXT fields include gateway/config/governance paths | met | `DiscoveryService::dnsTxtFields()` emits exactly the 11 keys the contract specifies (`product`, `role`, `gateway`, `mcp_path`, `config_path`=`/api/onboarding`, `governance_path`=`/api/governance/bundles`, `protovers`=`2025-03-26`, `auth`=`none`, `trust`=`lan`, `clu`=`true`, `forsetti`=`true`). The same map is used for all three service registrations. |
| Discovery JSON matches schema | met | `testWellKnownDocumentMatchesSchemaRequiredFields` pins the required-fields list from `discovery-document.schema.json`; `testDiscoveryDocumentJsonRoundTrip` pins gateway/onboarding/governance/capabilities content. The schema's `const` constraints (`product=MCOS`, `role=mcp-gateway-host`, `trust=lan`, `auth=none`) are enforced both by the struct defaults and by the `testDiscoveryDocumentDefaultShape` assertion set. |
| Beacon is gateway-first not provider-first | met | `BeaconService::start()` now broadcasts `discoveryService_->currentDocument()` JSON via UDP. The legacy provider-flavored `BeaconAdvertisement` shape no longer appears in the broadcast payload — it's only retained at `/api/beacon` for backward-compatibility with browsers that haven't migrated. |

## Risks and blockers

1. **Live LAN probe is unverified.** The DNS-SD records and the UDP beacon payload were not exercised against a real LAN in this dev environment. PHASE-04 (which produces the onboarding profiles a remote client uses) is the natural place to validate end-to-end discovery — once an operator runs `dns-sd -B _mcos._tcp local.` (or `mDNSResponder` equivalent) and confirms the records appear with the expected TXT fields. Static code review and unit tests cover the document shape and the registration request construction; what's deferred is the network round-trip.
2. **mDNS prerequisites on Windows hosts.** `DnsServiceRegister` requires the `Dnscache` service plus mDNS responder support. On hosts where it's missing, registration fails gracefully (per the same pattern as `PlatformServiceCatalogService`) and the runtime still serves `/.well-known/mcos.json` and the UDP beacon. Operators who need DNS-SD must install Bonjour or enable Windows mDNS — PHASE-10 (Windows hardening) is the right place to surface this prerequisite explicitly in the bootstrapper output.
3. **`/.well-known/` route prefix.** This is a non-`/api/` URL. The existing route handler in `MasterControlRuntime.cpp` already accommodates exact-path matches for `/api/health`, `/api/dashboard`, etc.; the new well-known route uses the same exact-path pattern and is positioned before the activity-stream filter so it isn't accidentally captured. Static review confirms; no automated route-table regression tests yet.
4. **`platformGateways` field overlap.** Resolved by naming: the new `DiscoveryDocument.gateway` is a single object (the MCP Gateway), distinct from the legacy `BeaconAdvertisement.platformGateways[]` array (Forsetti platform-services descriptors). Both can coexist without renaming.
5. **`instanceId` regeneration on every config rebuild.** A current limitation: `buildDefaultConfiguration()` returns a freshly-generated UUID on every call, so any code path that rebuilds the default config gets a new id. The persisted `mcos.json` retains its id once written, but any test or tool that calls `buildDefaultConfiguration()` repeatedly sees uniqueness (see `testInstanceIdGeneration`). PHASE-04 / PHASE-10 should consider whether to also persist instance-id stability to a separate file (e.g., `instance.id` next to `mcos.json`) so reinstalls preserve the operator-visible identifier.
6. **Pre-existing C4100 warning in `SetupWizardBuilder.cpp:133`.** Carries forward from PHASE-01; cosmetic; address at PHASE-09 / PHASE-10.

None of these block declaring PHASE-03 complete; they are forward-flagged for PHASE-04..PHASE-10.

## Deferred work

| Item | Deferred to | Reason |
|---|---|---|
| Live `dns-sd -B`/`avahi-browse` probe against a running MCOS | PHASE-04 / PHASE-10 | Requires running on a real LAN; dev environment lacks an mDNS observer. |
| `instance.id` persistence file (separate from `mcos.json`) | PHASE-04 or PHASE-10 | Stability across config regeneration is desirable but not required by ADR-002. |
| Bootstrapper surfacing of mDNS prerequisites | PHASE-10 | Hardening / release-gate concern. |
| Forsetti compliance script update for the new modules/routes | PHASE-05 / PHASE-10 | Per `.claude/rules/20-forsetti-clu-governance.md`, update only when architecture changes invalidate its assumptions. |
| Onboarding profiles consuming `DiscoveryDocument.onboarding.*` URLs | PHASE-04 | Phase file owns this. |
| Governance bundle URLs at `/api/governance/bundles/{platform}` | PHASE-05 | Phase file owns this. |
| Companion utility documentation (browse DNS-SD, fetch onboarding, write client config) | PHASE-04 | Phase exit criterion mentions this; landing point is PHASE-04. |

## Ready for next phase?

**Answer: yes** — `IDiscoveryService` is implemented and replaceable, the document shape is schema-locked, the three DNS-SD records are registered with the contract-specified TXT fields, the UDP beacon is gateway-first, and `/.well-known/mcos.json` plus `/api/discovery` are wired through the same code path. PHASE-04 has a stable foundation to build the onboarding profiles atop.

PHASE-04 should begin by:
1. Reading [handoff/realignment/PHASE-04-model-specific-onboarding-profiles.md](../../../handoff/realignment/PHASE-04-model-specific-onboarding-profiles.md) and its `readFirst` files (`resources/web/app.js`, `docs/wiki/Client-Config-Bundle.md`, `src/MasterControlApp/MasterControlRuntime.cpp`, `.mcp.json`).
2. Producing a file-by-file plan to add `OnboardingProfile` types matching `docs/implementation/schemas/onboarding-profile.schema.json`, an `IOnboardingProfileService`, profile composition for `claude-code` / `codex` / `grok` / `chatgpt` / `generic`, and `GET /api/onboarding/{clientType}` routes that consume `DiscoveryDocument.gateway.mcpUrl` and the governance bundle URL.
3. Adding tests pinning each profile's `gatewayMcpUrl` / `authRequired=false` / `governanceBundleUrl` / `configSnippets`.
4. Running `cmake --preset debug` / `cmake --build` / `ctest` end-to-end after the changes.
5. Stopping at the PHASE-04 completion report. Not proceeding to PHASE-05.

PHASE-03 stops here. No further phases will start without explicit instruction from the operator.
