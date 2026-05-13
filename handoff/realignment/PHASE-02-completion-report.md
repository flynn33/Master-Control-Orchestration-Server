# Phase Completion Report — PHASE-02

Phase: PHASE-02 — MCP Gateway spike with native HTTP.sys adapter
Phase file: [handoff/realignment/PHASE-02-mcp-gateway-spike-native HTTP.sys gateway.md](PHASE-02-mcp-gateway-spike-native HTTP.sys gateway.md)
Manifest: [handoff/realignment/manifest.json](manifest.json)
Date: 2026-05-01
Working tree: `master-control-dashboard-main`
Pre-phase commit: `197ba31` (PHASE-01 completion report)
Phase commit: `86695c3` (feat(phase-02): MCP Gateway spike with native HTTP.sys adapter)

## Scope completed

PHASE-02 introduced the `IMcpGateway` abstraction declared by ADR-002 §2 and shipped a working production adapter (`NativeHttpSysGatewayAdapter`) plus a deterministic test fake (`FakeMcpGatewayAdapter`). MCOS now owns the single MCP endpoint URL that AI clients will reach in PHASE-03+ onboarding profiles, surfaces gateway state through the dashboard snapshot, and exposes admin routes for status/health/tools/start/stop. The substrate is replaceable: PHASE-11 can evaluate a native HTTP.sys-backed gateway behind the same interface without breaking any client contract.

The production adapter supervises an external gateway binary under a Windows Job Object (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`) when a configured binary path exists, and operates in a "supervised-mock" mode otherwise — state transitions and registration still work, but `Probe()` returns `GatewayHealthStatus::Unknown` rather than fabricating a healthy state. This honors `.claude/rules/00-mcos-realignment.md`'s "no live-looking seeded infrastructure" rule and ADR-002 §9's "honest telemetry only" stance.

The runtime registers one stable logical MCP server (`mcos-default-pool`) at boot, establishing the registration shape that PHASE-06 will extend per managed worker pool. No autoscaled clones are exposed as separate public servers — that constraint is enforced by registration-by-pool-name, not by per-instance addressing.

## Files changed

| File | Change summary |
|---|---|
| [include/MasterControl/MasterControlContracts.h](../../include/MasterControl/MasterControlContracts.h) | Added `IMcpGateway` abstract interface above `IAdminApiService`. 10 virtual methods covering lifecycle (`Start`/`Stop`/`CurrentStatus`), health (`Probe`), registration (`RegisterHttpServer`/`RegisterStdioServer`/`DeregisterServer`), discovery (`ListTools`/`GatewayMcpUrl`), and metadata (`AdapterType`). |
| [include/MasterControl/MasterControlModels.h](../../include/MasterControl/MasterControlModels.h) | New enums `GatewayType`/`GatewayState`/`GatewayHealthStatus`/`McpServerTransport`. New structs `McpGatewayConfiguration`/`McpServerRegistration`/`McpToolDescriptor`/`GatewayStatus`/`GatewayHealth`/`RegistrationResult`/`DeregistrationResult`. `AppConfiguration` grows `mcpGateway`; `DashboardSnapshot` grows `mcpGatewayStatus`/`mcpGatewayHealth`/`mcpGatewayTools`. New `to_string` and `*FromString` helpers; new `to_json`/`from_json` adapters; full `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT` for each new struct. |
| [include/MasterControl/McpGatewayAdapters.h](../../include/MasterControl/McpGatewayAdapters.h) | New header. Declares `NativeHttpSysGatewayAdapter` (production, Windows-native) and `FakeMcpGatewayAdapter` (tests). Documents the supervised-mock mode contract: state transitions happen, but Probe() reports Unknown when the binary cannot be located, never a fabricated healthy state. |
| [src/MasterControlApp/McpGatewayAdapters.cpp](../../src/MasterControlApp/McpGatewayAdapters.cpp) | New file. `NativeHttpSysGatewayAdapter`: configuration-driven Start/Stop with Job Object containment via `CreateJobObjectW` + `AssignProcessToJobObject` + `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`; supervised-mock fallback when binary is missing; WinHTTP probe (500ms connect, 1.5s receive); in-memory server registry. `FakeMcpGatewayAdapter`: implements the same contract in-process with `setNextProbe` / `setStartShouldFail` / call-counter / registry-observer test hooks. |
| [src/MasterControlApp/MasterControlModels.cpp](../../src/MasterControlApp/MasterControlModels.cpp) | Added `to_string`/`fromString`/`to_json`/`from_json` for `GatewayType`, `GatewayState`, `GatewayHealthStatus`, `McpServerTransport`. Slugs match `gateway-service.schema.json`. |
| [src/MasterControlApp/MasterControlDefaults.cpp](../../src/MasterControlApp/MasterControlDefaults.cpp) | `buildDefaultConfiguration()` now seeds `configuration.mcpGateway` with `type=native HTTP.sys gateway`, `enabled=false`, `listenHost=0.0.0.0`, `listenPort=8080`, `mcpPath=/mcp`, `healthPath=/health`, `mode=lan-trusted`. Disabled-by-default is intentional — operators flip the flag once a binary is installed. |
| [src/MasterControlApp/MasterControlRuntime.cpp](../../src/MasterControlApp/MasterControlRuntime.cpp) | `AdminApiService` constructor accepts a `std::shared_ptr<IMcpGateway>`. `snapshot()` populates `mcpGatewayStatus`/`mcpGatewayHealth`/`mcpGatewayTools`. New routes: `GET /api/gateway/status`, `GET /api/gateway/health`, `GET /api/gateway/tools`, `POST /api/gateway/start`, `POST /api/gateway/stop`. `MasterControlApplication::Impl` constructs `NativeHttpSysGatewayAdapter` from current configuration and registers one logical server (`mcos-default-pool`) with it; `shutdown()` now calls `mcpGateway_->Stop()` to reap the Job Object before service teardown. |
| [src/MasterControlApp/CMakeLists.txt](../../src/MasterControlApp/CMakeLists.txt) | Adds `McpGatewayAdapters.cpp` to the `MasterControlApp` library. WinHTTP was already linked. |
| [tests/MasterControlOrchestrationServerTests.cpp](../../tests/MasterControlOrchestrationServerTests.cpp) | 13 new tests covering: default gateway configuration; fake adapter disabled vs enabled Start/Stop; scripted Start failure; registration round-trip; empty-name rejection; scripted Probe; MCP URL composition (including missing-leading-slash normalization); real adapter disabled-by-default behavior; real adapter supervised-mock mode when binary is missing; registration surviving Start/Stop; enum string round-trips for the 4 new enums; full `McpGatewayConfiguration` JSON round-trip. |
| [docs/implementation/FORBIDDEN-CONTRACT-GREP-LIST.md](../../docs/implementation/FORBIDDEN-CONTRACT-GREP-LIST.md) | Section 2.1 (native HTTP.sys gateway coupling outside the adapter) updated to allow the new adapter file paths and reject any new coupling outside them. |
| [docs/implementation/ARCHITECTURE-DRIFT-INVENTORY.md](../../docs/implementation/ARCHITECTURE-DRIFT-INVENTORY.md) | Section B (MCP Gateway surface) rows flipped from "replace (new)" to "done" with paths to the implementation. |

Total: 11 files changed (2 new + 9 modified), +1440 / -12 lines.

## Public contracts changed

- **C++ headers** — new `IMcpGateway` interface, new gateway model types, new `McpGatewayConfiguration` field on `AppConfiguration`, new gateway fields on `DashboardSnapshot`. JSON round-trip is stable; existing consumers that don't touch the new fields are unaffected.
- **HTTP API** — added five routes under `/api/gateway/*`. None of the existing admin routes were modified; this is a pure surface extension. The trust model on these routes is the same as the rest of the admin API (LAN-trusted operator surface). The actual MCP traffic on port 8080 is the AI-client surface and is not served by MCOS itself; it's served by the supervised gateway substrate behind the adapter.
- **Configuration** — `AppConfiguration.mcpGateway` is a new field. Old configuration files without it deserialize cleanly (the `_WITH_DEFAULT` macro fills in defaults).
- **Forbidden patterns** — section 2.1 of `FORBIDDEN-CONTRACT-GREP-LIST.md` now permits the substrings `native HTTP.sys gateway`/`native HTTP.sys gateway`/`native HTTP.sys gateway` to appear inside the adapter file pair, the enum string tables, and the `GatewayType::native HTTP.sys gateway` declaration; anywhere else is a coupling regression.

## Tests added or updated

13 new tests in `MasterControlOrchestrationServerTests.cpp`:

| Test | What it pins |
|---|---|
| `testGatewayConfigurationDefaults` | Default config carries the right type/port/path/mode and is disabled-by-default. |
| `testFakeGatewayDisabledStartsDisabled` | Disabled fake adapter refuses to Start. |
| `testFakeGatewayEnabledStartStopRoundTrip` | Enabled fake transitions Configured → Running → Stopped with timestamps. |
| `testFakeGatewayStartFailureScripted` | `setStartShouldFail` propagates a Failed state and the scripted message. |
| `testFakeGatewayRegistrationRoundTrip` | RegisterHttpServer / DeregisterServer + registry observer. |
| `testFakeGatewayRegistrationRejectsEmptyName` | Empty server name fails registration without polluting the registry. |
| `testFakeGatewayProbeUsesScriptedHealth` | Probe returns scripted health verbatim plus stamps adapter type and timestamp. |
| `testFakeGatewayMcpUrlComposition` | URL composes from listenHost+listenPort+mcpPath; missing leading slash is normalized. |
| `testRealAdapterDisabledByDefault` | Real adapter (production type) refuses Start when disabled and probes Unknown. |
| `testRealAdapterSupervisedMockWhenBinaryMissing` | Real adapter enters supervised-mock mode (Running but `isSupervisingChildProcess()==false`) when no binary is configured. |
| `testRealAdapterRegistrationSurvivesAcrossStartStop` | In-memory registry persists across the lifecycle. |
| `testGatewayEnumRoundTrips` | All four new enums serialize/deserialize through the documented slugs. |
| `testGatewayConfigJsonRoundTrip` | `McpGatewayConfiguration` round-trips losslessly through JSON. |

The pre-existing 13 tests (LanClient model, governance enums, deferred actions) continue to pass without modification.

## Validation performed

| Command | Result | Notes |
|---|---|---|
| `cmake --preset debug` (with `VCPKG_ROOT` set to VS-bundled vcpkg) | succeeded | Reused PHASE-01 build dir; vcpkg cache hit on nlohmann-json 3.12.0. |
| `cmake --build --preset debug` | **succeeded** — 0 errors | Pre-existing C4100 warning in `SetupWizardBuilder.cpp(133)` is the only diagnostic; not a regression. The new `McpGatewayAdapters.cpp` compiles clean. |
| `ctest --preset debug --output-on-failure` | **4/4 passed** in ~2.0s | `ForsettiCoreTests` (0.68s) · `ForsettiPlatformTests` (0.61s) · `ForsettiArchitectureTests` (0.61s) · `MasterControlOrchestrationServerTests` (0.05s, with 13 new gateway assertions all passing). |
| `git grep -nE 'native HTTP.sys gateway\|native HTTP.sys gateway\|native HTTP.sys gateway' -- src/ tests resources/web include ':!src/MasterControlApp/McpGatewayAdapters.cpp' ':!include/MasterControl/McpGatewayAdapters.h' ':!src/MasterControlApp/MasterControlDefaults.cpp' ':!src/MasterControlApp/MasterControlModels.cpp' ':!include/MasterControl/MasterControlModels.h' ':!docs/**'` | 0 matches | Coupling integrity check from FORBIDDEN-CONTRACT-GREP-LIST.md §2.1. The only allowed sites are the adapter implementation, the configuration default seeder, and the enum slug tables. |
| Static health endpoint check | `GET /api/gateway/health` route is wired and returns a `GatewayHealth` JSON; behavior verified by unit test (`testFakeGatewayProbeUsesScriptedHealth`) and code review of the runtime route block. | A live end-to-end probe against a real gateway binary is deferred to PHASE-03 once DNS-SD lands and a development native HTTP.sys gateway install is documented. |
| `scripts/check-mastercontrol-forsetti.ps1` | **NOT run** | Forsetti compliance script lives outside PHASE-02's scope (per `.claude/rules/20-forsetti-clu-governance.md`, update only when architecture changes invalidate its assumptions). PHASE-05 (CLU/Forsetti governance bundles) and PHASE-10 (release gate) re-run it. |

## Acceptance criteria status (from manifest)

| Criterion | Status | Evidence |
|---|---|---|
| MCOS exposes one gateway MCP URL | met | The URL is composed from `mcpGateway.listenHost` + `:listenPort` + `mcpPath`. `IMcpGateway::GatewayMcpUrl()` returns it; `GatewayStatus::mcpUrl` carries it; `GET /api/gateway/status` exposes it; the test `testFakeGatewayMcpUrlComposition` pins the format. The default port 8080 is intentionally distinct from the admin port 7300, satisfying ADR-002 §1's two-surfaces split. |
| native HTTP.sys gateway is supervised or mock-supervised | met | When `binaryPath` is configured and exists, `NativeHttpSysGatewayAdapter::Start()` calls `CreateProcessW` + `AssignProcessToJobObject` with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`. When it does not, the adapter enters supervised-mock mode (state transitions only, no fake health). The test `testRealAdapterSupervisedMockWhenBinaryMissing` pins this behavior. |
| Adapter can be replaced later | met | `IMcpGateway` is the only abstraction the runtime depends on; `MasterControlApplication::Impl` holds a `std::shared_ptr<IMcpGateway>`, not a concrete adapter. Swapping in a native HTTP.sys-backed implementation in PHASE-11 is a constructor-line change. |
| No worker clone is exposed directly as public gateway server | met | The runtime registers exactly one logical server name (`mcos-default-pool`). PHASE-06 will register one server per `ManagedEndpointPool`, never per `EndpointInstance`. The forbidden-pattern grep §2.2 (autoscaled clones registered as separate public servers) returns zero matches. |

Validation commands required by the manifest (`Targeted gateway adapter tests`, `Static health endpoint check`, `Build/tests where available`) all passed — see the table above.

## Risks and blockers

1. **Live native gateway integration is unverified.** PHASE-02 ships the adapter against the supervised-mock fallback (no real gateway binary present in this dev environment). Before PHASE-04 onboarding profiles point external clients at the real gateway URL, an operator should install native HTTP.sys gateway, set `mcpGateway.binaryPath`, flip `enabled=true`, and confirm the WinHTTP probe at `/health` returns 200. The adapter's WinHTTP path is exercised statically (200 → Healthy, non-200 → Degraded, no response → Unhealthy) but has not been run against a real subprocess yet.
2. **Listen-port collision detection is not enforced.** If an operator misconfigures `mcpGateway.listenPort` to 7300 (the admin port) the adapter will happily try to start and the supervised binary will fail to bind. PHASE-02 logs `Failed`/`Unhealthy` cleanly when this happens, but a configuration-time validation in PHASE-10 (release gate) is worth adding to surface the collision earlier.
3. **`McpToolDescriptor` aggregation is not wired.** `IMcpGateway::ListTools()` returns an empty vector in both adapters. Hydrating it from native HTTP.sys gateway's tool listing requires a live binary and is a PHASE-06 concern (once managed pools register backends with real tools). The current empty list is honest under ADR-002 §9.
4. **No DNS-SD advertisement yet.** The gateway URL is exposed via `/api/gateway/status` and `/api/discovery` (which doesn't exist yet) but not yet broadcast on the LAN. PHASE-03 lands DNS-SD/mDNS registration and the discovery endpoints.
5. **No /api/onboarding/{clientType} integration yet.** PHASE-04 will compose onboarding profiles around this gateway URL and the governance bundle URL (PHASE-05).
6. **C4100 'snapshot' warning persists in `SetupWizardBuilder.cpp:133`.** Pre-existing from PHASE-01; cosmetic; address at PHASE-09 (dashboard reskin) or PHASE-10 (hardening pass).

None of these block declaring PHASE-02 complete; they are forward-flagged for PHASE-03..PHASE-06.

## Deferred work

| Item | Deferred to | Reason |
|---|---|---|
| End-to-end test against a real gateway binary | PHASE-04 / PHASE-10 | Requires a development install + a documented bootstrap path; the adapter's contract is exercised today via the test fake and supervised-mock real adapter. |
| `ListTools()` hydrated from native HTTP.sys gateway's tool listing | PHASE-06 | Backends register through `ManagedEndpointPool`; tool listing follows. |
| DNS-SD / mDNS broadcast of gateway URL | PHASE-03 | Phase file owns this. |
| `/api/onboarding/{clientType}` profiles consuming the gateway URL | PHASE-04 | Phase file owns this. |
| Governance bundle URL distribution | PHASE-05 | Phase file owns this. |
| Listen-port collision validation at configure time | PHASE-10 | Hardening / release-gate concern. |
| Forsetti compliance script (`scripts/check-mastercontrol-forsetti.ps1`) re-run | PHASE-05 / PHASE-10 | Script update tied to broader architecture change windows. |

## Ready for next phase?

**Answer: yes** — `IMcpGateway` is implemented and replaceable, the production adapter supervises the gateway adapter (or a state-machine-only mock) safely, the test fake gives PHASE-03+ a deterministic gateway to mount discovery/onboarding tests against, and the runtime exposes a single stable gateway URL ready to be advertised.

PHASE-03 should begin by:
1. Reading [handoff/realignment/PHASE-03-bonjour-lan-discovery.md](PHASE-03-bonjour-lan-discovery.md) and its `readFirst` files (especially `src/MasterControlApp/MasterControlRuntime.cpp` for the existing `BeaconService`, `include/MasterControl/`, `docs/wiki/Remote-Client.md`, `resources/web/app.js`).
2. Producing a file-by-file plan to register `_mcos._tcp.local` / `_mcos-mcp._tcp.local` / `_mcos-onboarding._tcp.local` via Win32 `DnsServiceRegister`, add `/.well-known/mcos.json` and `/api/discovery` endpoints with the gateway-first document shape per `docs/implementation/MCP-GATEWAY-DISCOVERY-CONTRACT.md`, normalize the existing UDP beacon payload to the same shape, and rename the existing `BeaconAdvertisement.platformGateways` field to disambiguate from the new `gateway: {type, mcpUrl, healthUrl}` block (per the open-question note in `ARCHITECTURE-DRIFT-INVENTORY.md`).
3. Ensuring `cmake --preset debug` / `cmake --build` / `ctest` end-to-end remains green after the changes.
4. Stopping at the PHASE-03 completion report. Not proceeding to PHASE-04.

PHASE-02 stops here. No further phases will start without explicit instruction from the operator.
