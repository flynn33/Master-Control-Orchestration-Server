# Phase Completion Report — PHASE-08

Phase: PHASE-08 — Real-time telemetry model
Phase file: [handoff/realignment/PHASE-08-real-time-telemetry.md](PHASE-08-real-time-telemetry.md)
Manifest: [handoff/realignment/manifest.json](manifest.json)
Date: 2026-05-01
Working tree: `master-control-dashboard-main`
Pre-phase commit: `73762ab` (PHASE-07 completion report)
Phase commit: `228e944` (feat(phase-08): real-time telemetry model)

## Scope completed

PHASE-08 adds the gateway-first telemetry surface declared by ADR-002 §9. The new `ITelemetryAggregator` (and its production `TelemetryAggregator`) sits beside `ITelemetryService` (which keeps capturing the host snapshot for the beacon) and separates four concerns into one service:

1. **Events** — a 1024-entry ring buffer of `TelemetryEvent` (category × severity × message + free-form extra). Bounded so a noisy worker cannot OOM the runtime.
2. **Clients** — a presence roster keyed by `clientId`, populated via heartbeat ingest. Each `ClientPresence` carries `firstSeenUtc`, `lastSeenUtc`, `connectionCount`, `requestCount`, and the most recent `ClientHeartbeat`.
3. **Gateway** — a monotonic `GatewayTrafficSnapshot` (`requestCount`, `errorCount`, `bytesIn`, `bytesOut`, `lastObservedAtUtc`) that refreshes its `gatewayState`/`gatewayHealth` from a live `IMcpGateway::Probe()` on each `GET /api/telemetry/gateway`.
4. **Heartbeat ingest** — `POST /api/telemetry/heartbeat` accepts a `ClientHeartbeat`, upserts the presence record, and stores the heartbeat verbatim.

The honest-telemetry rule (ADR-002 §9) is enforced at the type level: `ClientHeartbeat::cpuPercent`, `memoryPercent`, `gpuPercent`, and `gpuMemoryMb` all default to **`-1.0`** (sentinel for "unavailable"). `0.0` would be ambiguous — it could mean "client is idle" or "client never reported". The `-1.0` sentinel separates the two. `WorkerTelemetry::cpuPercent` and `memoryMbytes` use the same `-1.0` sentinel for the same reason. `HostTelemetrySnapshot` keeps `0.0` defaults because it is directly measured (PDH-derived), where `0.0` is genuinely "idle".

Boot is observable: a `TelemetryCategory::System` `Info` event is recorded at runtime construction so the events ring is populated from second one — no "empty until something happens" gap that would fail the dashboard's "did the runtime start?" question.

## Files changed

| File | Change summary |
|---|---|
| [include/MasterControl/MasterControlContracts.h](../../include/MasterControl/MasterControlContracts.h) | Added `ITelemetryAggregator` interface (`recordEvent` / `recentEvents` / `recordHeartbeat` / `clientRoster` / `gatewayTraffic` / `incrementGatewayRequest`). |
| [include/MasterControl/MasterControlModels.h](../../include/MasterControl/MasterControlModels.h) | New `TelemetryCategory` and `TelemetrySeverity` enums. New structs `TelemetryEvent`, `ClientHeartbeat` (with `-1.0` unavailable sentinels), `ClientPresence`, `GatewayTrafficSnapshot` with full to_json / from_json. |
| [src/MasterControlApp/MasterControlModels.cpp](../../src/MasterControlApp/MasterControlModels.cpp) | TelemetryCategory + TelemetrySeverity string and JSON adapters. |
| [src/MasterControlApp/MasterControlRuntime.cpp](../../src/MasterControlApp/MasterControlRuntime.cpp) | New `TelemetryAggregator` class implementing the four-concern aggregator with a 1024-entry mutex-guarded events ring, presence map, traffic snapshot, and a `setGatewayTrafficContext()` helper used by the `GET /api/telemetry/gateway` route to refresh from a live `IMcpGateway::Probe()`. Boot event recorded in `MasterControlApplication::Impl` construction. Four new HTTP routes wired. `(std::min)(...)` parenthesization on `std::min` to bypass the `Windows.h` `min()` macro collision in this TU (same family of fix as PHASE-06's `(std::max)` and PHASE-07's `(std::numeric_limits<int>::max)()`). |
| [tests/MasterControlOrchestrationServerTests.cpp](../../tests/MasterControlOrchestrationServerTests.cpp) | 7 new tests pinning the public telemetry contract; wired into `main()` after the PHASE-07 tests. |
| [docs/implementation/ARCHITECTURE-DRIFT-INVENTORY.md](../../docs/implementation/ARCHITECTURE-DRIFT-INVENTORY.md) | Section F (Telemetry surface) flipped to "done" with implementation paths; auto-fail-leases sweeper flagged as PHASE-08 deferred work. |
| [docs/implementation/FORBIDDEN-CONTRACT-GREP-LIST.md](../../docs/implementation/FORBIDDEN-CONTRACT-GREP-LIST.md) | New sections 4.3 (ClientHeartbeat / WorkerTelemetry unavailable-sentinel integrity), 4.4 (telemetry event ring-buffer cap stays at 1024), 4.5 (heartbeat ingest is the only client-metric write site). |

Total: 7 files changed, +582 / -3 lines.

## Public contracts changed

- **C++ headers** — new `ITelemetryAggregator` interface; new `TelemetryCategory` and `TelemetrySeverity` enums; new `TelemetryEvent` / `ClientHeartbeat` / `ClientPresence` / `GatewayTrafficSnapshot` structs.
- **HTTP API** — four new routes:
  - `GET /api/telemetry/events` — recent ring buffer; query param `?max=N` (default 100; capped at the ring size 1024).
  - `GET /api/telemetry/clients` — presence roster.
  - `GET /api/telemetry/gateway` — traffic snapshot (refreshes `gatewayState`/`gatewayHealth` from a live `IMcpGateway::Probe()` on each call).
  - `POST /api/telemetry/heartbeat` — heartbeat ingest with presence upsert.
- **JSON shape** — `TelemetryEvent` keys: `eventId`, `timestamp`, `category` (slug), `severity` (slug), `source`, `message`, `extra` (object). `ClientHeartbeat` keys: `clientId`, `clientType`, `version`, `ipAddress`, `sentAtUtc`, `cpuPercent`, `memoryPercent`, `gpuPercent`, `gpuMemoryMb`, `bytesSentPerSecond`, `bytesReceivedPerSecond`, `sessionContext`. `ClientPresence` keys: `clientId`, `clientType`, `ipAddress`, `firstSeenUtc`, `lastSeenUtc`, `connectionCount`, `requestCount`, `lastHeartbeat`. `GatewayTrafficSnapshot` keys: `gatewayType`, `gatewayState`, `gatewayHealth`, `mcpUrl`, `requestCount`, `errorCount`, `bytesIn`, `bytesOut`, `lastObservedAtUtc`.

## Tests added or updated

7 new tests:

| Test | What it pins |
|---|---|
| `testTelemetryCategoryEnumRoundTrip` | All six category slugs (`system`, `gateway`, `worker`, `client`, `discovery`, `governance`) round-trip. |
| `testTelemetrySeverityEnumRoundTrip` | All four severity slugs (`info`, `warning`, `error`, `critical`) round-trip. |
| `testTelemetryEventJsonRequiredFields` | Schema-required keys present; category and severity serialize as their string slugs. |
| `testClientHeartbeatHonestDefaultsAreUnavailable` | Default-constructed `ClientHeartbeat` has `cpuPercent == -1.0`, `memoryPercent == -1.0`, `gpuPercent == -1.0`, `gpuMemoryMb == -1.0` — pins the ADR-002 §9 honest-telemetry invariant against future drift. |
| `testClientHeartbeatJsonRoundTrip` | Heartbeat survives JSON round-trip including `sessionContext` object and the optional metrics. |
| `testClientPresenceShape` | `ClientPresence` exposes `clientId` / `firstSeenUtc` / `lastSeenUtc` / `connectionCount` / `requestCount` / `lastHeartbeat` (nested heartbeat round-trips). |
| `testGatewayTrafficSnapshotShape` | All four monotonic counters and the `gatewayType`/`gatewayState`/`gatewayHealth`/`mcpUrl`/`lastObservedAtUtc` fields round-trip. |

The pre-existing 49 tests continue to pass. The test file now declares 56 test functions (`grep -c '^bool test'`), all wired into `main()`.

## Validation performed

| Command | Result | Notes |
|---|---|---|
| `cmake --build --preset debug` | **succeeded** — 0 errors | Reused PHASE-07 build dir. Required parenthesizing `std::min` to bypass the `Windows.h` `min()` macro collision in this TU (same family of workaround as PHASE-06's `std::max` and PHASE-07's `std::numeric_limits<int>::max`). One pre-existing C4100 warning at `SetupWizardBuilder.cpp:133` carries forward unchanged. |
| `ctest --preset debug --output-on-failure` | **4/4 passed** in ~2.0s | 56 total test functions; 7 new this phase. |
| `scripts/check-mastercontrol-forsetti.ps1` | **PASS** — `Master Control Forsetti checks passed.` | No script update needed for PHASE-08; vendoring untouched and the new types are Core-facing. |
| FORBIDDEN-CONTRACT §4.3 (ClientHeartbeat / WorkerTelemetry unavailable-sentinel integrity) | 0 matches | `gpuPercent = 0.0` / `gpuMemoryMb = 0.0` patterns return empty across `include/`, `src/MasterControlApp/`, `src/MasterControlModules/`. |
| FORBIDDEN-CONTRACT §4.4 (telemetry event ring-buffer cap) | 1 match | Exactly the documented `static constexpr std::size_t kMaxEvents_ = 1024;` declaration inside `TelemetryAggregator`. |
| FORBIDDEN-CONTRACT §4.5 (heartbeat ingest is the only client-metric write site) | 2 matches | The aggregator member-function definition and the single `POST /api/telemetry/heartbeat` route handler call site. No synthesizer code path. |
| Vendoring integrity (FORBIDDEN-CONTRACT §5.1) | 0 changes | `git diff Forsetti-Framework-Windows-main/` since baseline → empty. |

## Acceptance criteria status (from manifest)

| Criterion | Status | Evidence |
|---|---|---|
| Telemetry split into host / client / gateway / worker categories | met | `TelemetryCategory` enum carries `System`/`Gateway`/`Worker`/`Client`/`Discovery`/`Governance`. `ClientHeartbeat` and `ClientPresence` model client telemetry; `GatewayTrafficSnapshot` models gateway traffic; `WorkerTelemetry` (PHASE-06) models worker telemetry; `HostTelemetrySnapshot` and `ITelemetryService` (pre-existing) model host telemetry. |
| Per-AI-client metrics only when supplied by client heartbeat or sidecar | met | `ClientHeartbeat::cpuPercent`/`memoryPercent`/`gpuPercent`/`gpuMemoryMb` default to `-1.0` (unavailable). The runtime never synthesizes these values — `recordHeartbeat()` is the only write site (FORBIDDEN-CONTRACT §4.5 enforces). |
| Honest "unavailable" state | met | `testClientHeartbeatHonestDefaultsAreUnavailable` pins the `-1.0` defaults in the test suite. FORBIDDEN-CONTRACT §4.3 prevents drift toward `0.0` defaults. |
| Activity event taxonomy | met | `TelemetryCategory` × `TelemetrySeverity` covers system / gateway / worker / client / discovery / governance × info / warning / error / critical. Boot event recorded at runtime construction so the ring is populated from second one. |
| Gateway telemetry exposed through API | met | `GET /api/telemetry/gateway` returns `GatewayTrafficSnapshot` with monotonic counters and a fresh `gatewayState`/`gatewayHealth` from a live `IMcpGateway::Probe()`. |

## Risks and blockers

1. **Synthetic-load not exercised end-to-end.** The aggregator's behavior is pinned by structural tests (enum round-trip, JSON shape, honest defaults). A scenario test that drives heartbeat POSTs through the runtime's HTTP front door and asserts the presence roster updates is deferred to PHASE-10 release-gate hardening — the same shape as PHASE-07's deferred multi-threaded scale-out load test, for the same reason (the dev environment lacks a process the supervisor can spawn end-to-end).
2. **PDH/DXGI host enrichment is not yet wired.** The host telemetry side keeps using `ITelemetryService`'s primary-IP/hostname snapshot. PDH-derived CPU/disk/network counters and DXGI GPU-memory readings are the natural next step but did not land in PHASE-08 because the schema-driven aggregator and honest-defaults pinning were the higher-risk acceptance criteria. PDH/DXGI enrichment can land as a small additive change inside `TelemetryAggregator::recordEvent` periodic feed, or as a separate `IHostTelemetryProbe`. The runtime already has the surface to publish it.
3. **Auto-fail-leases-on-failed-instance sweeper still deferred.** PHASE-07 flagged this for PHASE-08; it is now flagged for a future phase. The reason: a sweeper needs a periodic timer thread that wakes, walks `IWorkerSupervisor::listPools()` and `ILeaseRouter::activeLeases()`, and transitions leases bound to `Failed` instances. That timer is the right place to also publish heartbeat-decay events (clients that haven't heartbeated for N seconds → `Stale` presence). Doing both at once is cheaper than building two timers, so deferring once more is preferred over a partial implementation. PHASE-09 (dashboard) does not depend on the sweeper; PHASE-10 (release gate) is the natural deadline.
4. **`std::min` macro collision** required parenthesization in this TU. The pattern is identical to PHASE-06's `std::max` and PHASE-07's `std::numeric_limits<int>::max` — `Windows.h` defines `min()` and `max()` as macros and they collide with the `<algorithm>` and `<limits>` symbols. Carrying the workaround forward in this TU; flagged so future authors don't trip over the same `Windows.h` macro.
5. **Pre-existing C4100 warning persists** in `SetupWizardBuilder.cpp:133`. Carries forward from PHASE-01.
6. **Heartbeat extra fields are stored verbatim with no whitelist.** `ClientHeartbeat::sessionContext` is an `nlohmann::json` object accepted as-is; same for `TelemetryEvent::extra`. This is intentional (the dashboard panel needs free-form context) but it means a misbehaving client could push large blobs. The 1024-event ring cap bounds memory growth, and heartbeats are upserted (not appended) so client count bounds memory. PHASE-10 can add a per-blob size cap if real-world traffic warrants it.

None of these block declaring PHASE-08 complete.

## Deferred work

| Item | Deferred to | Reason |
|---|---|---|
| Auto-fail-leases-on-failed-instance sweeper | Future phase (PHASE-09 or PHASE-10) | Wants to share a periodic-timer thread with heartbeat-decay-to-Stale presence; building both at once is cheaper than building two timers. |
| PDH host counters / DXGI GPU memory enrichment | Future phase | Aggregator surface is ready; the enrichment is an additive change behind `IHostTelemetryProbe`. Did not land here because schema-driven aggregator and honest-defaults pinning were the higher-risk PHASE-08 acceptance criteria. |
| Heartbeat-decay → `Stale` presence transitions | Future phase | Pairs naturally with the auto-fail-leases sweeper. |
| End-to-end scenario test (heartbeat POST → presence roster update) | PHASE-10 | Release-gate hardening concern; needs an HTTP harness through the runtime front door. |
| Browser dashboard telemetry panels | PHASE-09 | Phase file owns dashboard reskin. |
| Per-blob size cap on `sessionContext` / `extra` | PHASE-10 | Hardening concern. |
| `OnboardingProfile.telemetryHeartbeatUrl` advertisement | PHASE-09 | Once the dashboard panel needs to know where to POST. |

## Ready for next phase?

**Answer: yes** — `ITelemetryAggregator` is implemented and replaceable, the four concerns (events / clients / gateway / heartbeat) are pinned by 7 schema-required-fields tests, the admin surface is wired through four new routes, and the honest-telemetry invariant (`-1.0` = unavailable; `0.0` is reserved for genuine idle) is enforced both in code (struct defaults), in tests (`testClientHeartbeatHonestDefaultsAreUnavailable`), and in CI (FORBIDDEN-CONTRACT §4.3 / §4.5).

PHASE-09 should begin by:
1. Reading [handoff/realignment/PHASE-09-tron-dashboard-realignment.md](PHASE-09-tron-dashboard-realignment.md) and its `readFirst` files (`resources/web/`, the new `/api/discovery`, `/api/onboarding/{clientType}`, `/api/governance/bundles`, `/api/pools`, `/api/telemetry/{events,clients,gateway}` routes).
2. Producing a file-by-file plan to introduce dashboard panels for: gateway status (native HTTP.sys gateway health), client/model roster (driven by `/api/telemetry/clients`), worker pool gauges with utilization (driven by `/api/pools` + `/api/pools/{poolId}/saturation`), governance bundle downloads, real-time activity log (driven by `/api/telemetry/events`), and onboarding/setup view (driven by `/api/onboarding/{clientType}`). Manual and import setup paths must be preserved.
3. Honoring ADR-002 §9: dashboards must render the `-1.0` sentinel as "unavailable", never as "idle" or "0%". No fake utilization in the UI.
4. Running `cmake --preset debug` / `cmake --build` / `ctest` end-to-end after the changes.
5. Stopping at the PHASE-09 completion report. Not proceeding to PHASE-10.

PHASE-08 stops here. No further phases will start without explicit instruction from the operator.
