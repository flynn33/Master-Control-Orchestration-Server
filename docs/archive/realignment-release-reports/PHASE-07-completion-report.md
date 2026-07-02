# Phase Completion Report — PHASE-07

Phase: PHASE-07 — Autoscaling and lease routing
Phase file: [handoff/realignment/PHASE-07-autoscaling-lease-routing.md](../../../handoff/realignment/PHASE-07-autoscaling-lease-routing.md)
Manifest: [handoff/realignment/manifest.json](../../../handoff/realignment/manifest.json)
Date: 2026-05-01
Working tree: `master-control-dashboard-main`
Pre-phase commit: `c6cd050` (PHASE-06 completion report)
Phase commit: `0cb9b48` (feat(phase-07): autoscaling and lease routing)

## Scope completed

PHASE-07 added the lease + autoscale layer declared by ADR-002 §8. The new `ILeaseRouter` (and its production `LeaseRouter`) sit on top of PHASE-06's `IWorkerSupervisor` and resolve `LeaseRequest`s into concrete `EndpointLease`s using sticky-session routing for stateful sessions and least-loaded selection for stateless ones. When all Ready instances are at `maxActiveLeasesPerInstance` and the pool is still below `maxInstances`, the router triggers a same-type scale-out via the new `IWorkerSupervisor::scaleUpOnce` and binds the new lease to the freshly-spawned instance. Hot-migration of active stateful streams is forbidden; FORBIDDEN-CONTRACT §2.4 verifies the sticky-session integrity invariant via a grep regression detector.

The runtime now exposes the lease admin surface through four routes:
- `POST /api/pools/{poolId}/leases` (acquire; LeaseRequest body optional)
- `POST /api/leases/{leaseId}/release`
- `GET /api/pools/{poolId}/leases` (active list, sorted by acquisition time)
- `GET /api/pools/{poolId}/saturation` (PoolSaturation with `atSaturation` / `atMaxInstances` flags + counts)

`drainPool` from PHASE-06 already transitions instances to `Draining`; PHASE-07's selection rule skips Draining instances when looking for headroom, so stateful sessions stay sticky on their original instance while new sessions route elsewhere — matching the ADR-002 §8 drain rule exactly.

## Files changed

| File | Change summary |
|---|---|
| [include/MasterControl/MasterControlContracts.h](../../../include/MasterControl/MasterControlContracts.h) | Added `ILeaseRouter` interface (`acquireLease` / `releaseLease` / `activeLeases` / `saturationFor`). Extended `IWorkerSupervisor` with `scaleUpOnce(poolId)` so the router can trigger same-type scale-out without bypassing the supervisor's lifecycle ownership. |
| [include/MasterControl/MasterControlModels.h](../../../include/MasterControl/MasterControlModels.h) | New `LeaseState` enum (`Active`/`Released`/`Failed`); new `LeaseRequest`, `EndpointLease`, `PoolSaturation` structs with full to_json/from_json. |
| [src/MasterControlApp/MasterControlModels.cpp](../../../src/MasterControlApp/MasterControlModels.cpp) | LeaseState string + JSON adapters. |
| [src/MasterControlApp/MasterControlRuntime.cpp](../../../src/MasterControlApp/MasterControlRuntime.cpp) | New `LeaseRouter` class implementing the four-step selection rule (sticky → least-loaded → scale-out → fail). `WorkerSupervisor::scaleUpOnce` implementation. Four new HTTP routes. Wired into runtime construction + shutdown. `(std::numeric_limits<int>::max)()` parenthesization to bypass the `Windows.h` `max()` macro collision in this TU. |
| [tests/MasterControlOrchestrationServerTests.cpp](../../../tests/MasterControlOrchestrationServerTests.cpp) | 6 new tests pinning the lease + saturation contract. |
| [docs/implementation/ARCHITECTURE-DRIFT-INVENTORY.md](../../implementation/ARCHITECTURE-DRIFT-INVENTORY.md) | Section E rows for lease routing, autoscaling, and the lease admin surface flipped to "done". |
| [docs/implementation/FORBIDDEN-CONTRACT-GREP-LIST.md](../../implementation/FORBIDDEN-CONTRACT-GREP-LIST.md) | New section 2.4 — sticky-session integrity grep enforcing the no-hot-migration rule. |

Total: 7 files changed, +567 / -5 lines.

## Public contracts changed

- **C++ headers** — new `ILeaseRouter` interface; new `LeaseState` enum and `LeaseRequest` / `EndpointLease` / `PoolSaturation` types; `IWorkerSupervisor` extended with `scaleUpOnce`.
- **HTTP API** — four new routes under `/api/pools/{poolId}/leases`, `/api/pools/{poolId}/saturation`, and `/api/leases/{leaseId}/release`.
- **JSON shape** — `LeaseRequest` uses lowercase keys (`poolId`, `sessionId`, `clientHint`, `stateful`); `EndpointLease` carries the `state` slug, `acquiredAtUtc`/`releasedAtUtc` timestamps, and `statusMessage`. `PoolSaturation` exposes the three flags (`atSaturation`, `scaleOutTriggered`, `atMaxInstances`) plus 5 counts.

## Tests added or updated

6 new tests:

| Test | What it pins |
|---|---|
| `testLeaseStateEnumRoundTrip` | All three lifecycle slugs (`active`, `released`, `failed`) round-trip. |
| `testLeaseRequestJsonRoundTrip` | LeaseRequest JSON shape preserves poolId / sessionId / stateful flag. |
| `testEndpointLeaseDefaultStateActive` | Default-constructed lease starts `Active`. |
| `testEndpointLeaseJsonShape` | State slug + bound `instanceId` + sticky `sessionId` survive JSON. |
| `testPoolSaturationJsonShape` | Saturation flags + 5 counts (instanceCount / readyInstanceCount / drainingInstanceCount / activeLeaseCount / queueDepth) all serialize. |
| `testScalePolicyDefaultsAreSafe` | Defaults DO NOT auto-spawn (ADR-002 §9): `minInstances=0`, `maxInstances>=1`, `maxActiveLeasesPerInstance>=1`. |

The pre-existing 43 tests continue to pass.

## Validation performed

| Command | Result | Notes |
|---|---|---|
| `cmake --preset debug` | succeeded | Reused PHASE-06 build dir. |
| `cmake --build --preset debug` | **succeeded** — 0 errors | Required parenthesizing `std::numeric_limits<int>::max` to bypass the `Windows.h` `max()` macro collision (same pattern PHASE-06 hit with `std::max`). |
| `ctest --preset debug --output-on-failure` | **4/4 passed** in ~2.0s | 49 total test functions; 6 new this phase. |
| `scripts/check-mastercontrol-forsetti.ps1` | **PASS** — `Master Control Forsetti checks passed.` | No script update needed for PHASE-07. |
| FORBIDDEN-CONTRACT §2.3 (hot-migration) | 0 matches | `git grep migrateSession\|hotMigrate\|migrateLease` returns empty. |
| FORBIDDEN-CONTRACT §2.4 (sticky-session writes) | matches only inside `LeaseRouter::bindLeaseLocked` (new-lease bind), `LeaseRouter::acquireLease` (stale-entry cleanup), and `LeaseRouter::releaseLease` (release cleanup). | All three are documented sites; no other write site exists. |
| Vendoring integrity (FORBIDDEN-CONTRACT §1.7c) | 0 changes | `git diff Forsetti-Framework-Windows-main/` since baseline → empty. |

## Acceptance criteria status (from manifest)

| Criterion | Status | Evidence |
|---|---|---|
| Heavy utilization triggers same-type scale-out | met | `LeaseRouter::acquireLease` step 3 calls `IWorkerSupervisor::scaleUpOnce(poolId)` when `selectLeastLoadedReadyLocked` returns `nullopt` (all Ready instances at `maxActiveLeasesPerInstance`). The new instance is bound to the new lease. `PoolSaturation.scaleOutTriggered` reports the event for dashboard observers. |
| Existing stateful sessions drain/stick | met | Sticky lookup (step 1) returns the existing lease verbatim for any sessionId already mapped. The least-loaded selector (step 2) skips `Draining` instances, so stateful sessions on a draining instance keep their sticky lease while new sessions route to non-draining Ready instances. |
| New sessions route to scaled instance | met | Step 3 binds the new lease to the instanceId returned by `scaleUpOnce`. The lease's `statusMessage` reads "Lease bound to freshly-spawned instance after pool scale-out." |
| Dashboard/API show pool saturation | met | `GET /api/pools/{poolId}/saturation` returns `PoolSaturation` with all five counts and three flags. The dashboard panel for this lands in PHASE-09. |

## Risks and blockers

1. **Synthetic load tests run in-process only.** Tests pin the lease/saturation JSON shape and the lifecycle invariants. A multi-threaded synthetic load harness (spinning leases concurrently to verify the scale-out trigger fires) is deferred to PHASE-10 release-gate hardening — the dev environment doesn't yet have a process the supervisor can actually spawn end-to-end.
2. **Failure/restart paths are minimal.** `LeaseState::Failed` is reachable via the saturation-at-maxInstances path. The supervisor side has `EndpointInstanceState::Failed` from the `CreateProcessW` failure path. Wiring failed-instance leases back to a `Failed` lease state automatically (so the router can re-bind) is a PHASE-08 telemetry concern.
3. **No cross-instance lease re-binding on instance failure.** If an instance fails mid-lease, the lease stays `Active` until the caller explicitly releases it. The contract is correct (no hot-migration), but PHASE-08 should add a periodic sweeper that transitions stale leases on `Failed` instances to `Failed` automatically.
4. **No real backpressure.** `PoolSaturation.queueDepth` is 0 today. PHASE-08 telemetry will populate it from real instance metrics; PHASE-07 honestly reports 0 rather than fabricating a value (ADR-002 §9).
5. **Pre-existing C4100 warning persists** in `SetupWizardBuilder.cpp:133`. Carries forward from PHASE-01.

None of these block declaring PHASE-07 complete.

## Deferred work

| Item | Deferred to | Reason |
|---|---|---|
| End-to-end multi-threaded scale-out load test | PHASE-10 | Requires a real spawnable backend; release-gate hardening concern. |
| Auto-fail-leases-on-failed-instance sweeper | PHASE-08 | Telemetry concern. |
| Real `queueDepth` from instance backpressure | PHASE-08 | Phase file owns telemetry. |
| Browser dashboard pool saturation panel | PHASE-09 | Phase file owns dashboard reskin. |
| `LeaseRouter` persistence across restarts | Future | In-memory only is correct for PHASE-07; persistence concern for production-readiness post-PHASE-10. |

## Ready for next phase?

**Answer: yes** — `ILeaseRouter` is implemented and replaceable, the four-step selection rule honors all three ADR-002 §8 invariants (sticky for stateful, least-loaded for stateless, scale-out under saturation, no hot-migration), the admin surface is wired through four new routes, and the schema-required-fields tests pin JSON conformance. FORBIDDEN-CONTRACT §2.4 enforces sticky-session integrity at grep level.

PHASE-08 should begin by:
1. Reading [handoff/realignment/PHASE-08-real-time-telemetry.md](../../../handoff/realignment/PHASE-08-real-time-telemetry.md) and its `readFirst` files.
2. Producing a file-by-file plan to introduce per-instance telemetry (PDH-derived CPU/memory/I/O), client heartbeat schema, gateway telemetry, an activity event taxonomy, and the PHASE-07-flagged auto-fail-leases-on-failed-instance sweeper.
3. Honoring ADR-002 §9: per-AI-client CPU/GPU/disk metrics only when supplied by client heartbeat or sidecar; otherwise honest "unavailable" state. No fake utilization.
4. Running `cmake --preset debug` / `cmake --build` / `ctest` end-to-end after the changes.
5. Stopping at the PHASE-08 completion report. Not proceeding to PHASE-09.

PHASE-07 stops here. No further phases will start without explicit instruction from the operator.
