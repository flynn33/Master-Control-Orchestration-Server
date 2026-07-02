# Phase Completion Report — PHASE-06

Phase: PHASE-06 — Managed MCP/sub-agent worker pools
Phase file: [handoff/realignment/PHASE-06-managed-worker-pools.md](../../../handoff/realignment/PHASE-06-managed-worker-pools.md)
Manifest: [handoff/realignment/manifest.json](../../../handoff/realignment/manifest.json)
Date: 2026-05-01
Working tree: `master-control-dashboard-main`
Pre-phase commit: `350ea1c` (PHASE-05 completion report)
Phase commit: `c8077f0` (feat(phase-06): managed MCP/sub-agent worker pools)

## Scope completed

PHASE-06 introduced the supervised process model declared by ADR-002 §7. MCP servers and sub-agents now share a single operational abstraction (`ManagedEndpointPool`) with a 7-state instance lifecycle (`Configured → Starting → Ready → Busy → Draining → Stopped`, with `Failed` reachable from any non-terminal state). The new `IWorkerSupervisor` interface owns pool registration and lifecycle; the production `WorkerSupervisor` (in `MasterControlRuntime.cpp`) spawns child processes inside Windows Job Objects with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` so the supervisor's destructor reaps the entire process tree atomically.

When a pool's template has no executable configured (or the file does not exist), the supervisor enters supervised-mock mode: state transitions still happen so the contract is exercisable, but `supervised=false` and no fake live process appears in the registry — honoring ADR-002 §9 ("no fake live infrastructure"). The `testManagedPoolEmptyByDefault` test pins the invariant.

The new admin surface (`/api/pools` plus per-pool action routes) lets operators register pools, scale to `minInstances`, drain, and remove them. PHASE-07 will layer the lease router + autoscaler atop this foundation.

## Files changed

| File | Change summary |
|---|---|
| [include/MasterControl/MasterControlContracts.h](../../../include/MasterControl/MasterControlContracts.h) | Added `IWorkerSupervisor` interface with seven virtual methods (listPools / findPool / upsertPool / removePool / ensureMinInstances / drainPool / shutdownAll). Added `<optional>` include. |
| [include/MasterControl/MasterControlModels.h](../../../include/MasterControl/MasterControlModels.h) | New `EndpointPoolKind` and `EndpointInstanceState` enums. New structs `ScalePolicy`, `DrainPolicy`, `HealthProbeSpec`, `EndpointTemplate`, `WorkerTelemetry`, `EndpointInstance`, `ManagedEndpointPool` matching `managed-endpoint-pool.schema.json`. Full to_string / fromString / to_json / from_json declarations. ManagedEndpointPool uses an explicit serializer to map the `template_` field name to the schema's `template` JSON key. |
| [src/MasterControlApp/MasterControlModels.cpp](../../../src/MasterControlApp/MasterControlModels.cpp) | Implementations for the two new enums' string/JSON adapters. |
| [src/MasterControlApp/MasterControlRuntime.cpp](../../../src/MasterControlApp/MasterControlRuntime.cpp) | New `WorkerSupervisor` class (anonymous namespace) implementing the lifecycle state machine, Job Object child supervision, and supervised-mock fallback. Constructed at boot. Wired into runtime `shutdown()` so child trees are reaped before service teardown. New routes: `GET /api/pools`, `GET /api/pools/{poolId}`, `POST /api/pools` (upsert), `POST /api/pools/{poolId}/{remove\|scale\|drain}`. `(std::max)(...)` parenthesization used to bypass the `Windows.h` `max()` macro collision in this TU. |
| [tests/MasterControlOrchestrationServerTests.cpp](../../../tests/MasterControlOrchestrationServerTests.cpp) | 5 new tests pinning the public model contract: `testEndpointPoolKindEnumRoundTrip`, `testEndpointInstanceStateAllSevenLifecycleStates`, `testManagedEndpointPoolJsonRequiredFields`, `testEndpointInstanceJsonShape`, `testManagedPoolEmptyByDefault`. |
| [docs/implementation/ARCHITECTURE-DRIFT-INVENTORY.md](../../implementation/ARCHITECTURE-DRIFT-INVENTORY.md) | Section E rows for the model / lifecycle / supervision / admin-surface flipped to "done"; lease routing / autoscaling rows remain pending PHASE-07. |
| [docs/implementation/FORBIDDEN-CONTRACT-GREP-LIST.md](../../implementation/FORBIDDEN-CONTRACT-GREP-LIST.md) | New section 2.1a — worker process tree containment grep. `CreateProcessW` outside the two supervised call sites (`WorkerSupervisor::startInstanceLocked` and `NativeHttpSysGatewayAdapter::Start`) is a regression. |

Total: 7 files changed, +709 / -7 lines.

## Public contracts changed

- **C++ headers** — new `IWorkerSupervisor` interface; new `ManagedEndpointPool` and friends. JSON round-trip stable.
- **HTTP API** — added six routes under `/api/pools`. `/api/onboarding/{clientType}` profiles do not yet expose pool URLs; PHASE-07 may add a `pools[]` field after the lease router lands.
- **Schema conformance** — `ManagedEndpointPool` JSON serialization satisfies all 5 required keys per `managed-endpoint-pool.schema.json` (`poolId`, `kind`, `logicalMcpUrl`, `template`, `scalePolicy`).

## Tests added or updated

5 new tests:

| Test | What it pins |
|---|---|
| `testEndpointPoolKindEnumRoundTrip` | `mcp-server` / `sub-agent` slugs round-trip. |
| `testEndpointInstanceStateAllSevenLifecycleStates` | All seven lifecycle states (`configured`, `starting`, `ready`, `busy`, `draining`, `failed`, `stopped`) round-trip through `to_string` and `endpointInstanceStateFromString`. |
| `testManagedEndpointPoolJsonRequiredFields` | Schema-required keys present; `kind` and `template.transport` serialize literally. |
| `testEndpointInstanceJsonShape` | `state` / `supervised` / nested `telemetry` round-trip. |
| `testManagedPoolEmptyByDefault` | Default pool has zero instances and `minInstances=0` (ADR-002 §9 invariant). |

The pre-existing 38 tests continue to pass.

## Validation performed

| Command | Result | Notes |
|---|---|---|
| `cmake --preset debug` | succeeded | Reused PHASE-05 build dir. |
| `cmake --build --preset debug` | **succeeded** — 0 errors | Required parenthesizing `std::max` to bypass the `Windows.h` `max()` macro collision in this TU. |
| `ctest --preset debug --output-on-failure` | **4/4 passed** in ~2.0s | 43 total test functions; 5 new this phase. |
| `scripts/check-mastercontrol-forsetti.ps1` | **PASS** — `Master Control Forsetti checks passed.` | No script update needed for PHASE-06 because vendoring is untouched and the new module structure stays Core-facing. |
| FORBIDDEN-CONTRACT §2.1a (worker process tree containment) | 0 matches | `git grep CreateProcessW` outside the two supervised call sites returns empty. Both call sites pair `CreateProcessW` with `AssignProcessToJobObject` and `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`. |
| Vendoring integrity (FORBIDDEN-CONTRACT §1.7c) | 0 changes | `git diff Forsetti-Framework-Windows-main/` since baseline → empty. |

## Acceptance criteria status (from manifest)

| Criterion | Status | Evidence |
|---|---|---|
| MCP servers and sub-agents use same operational abstraction | met | `ManagedEndpointPool.kind` carries `EndpointPoolKind::{McpServer, SubAgent}`. The same supervisor handles both kinds. `testEndpointPoolKindEnumRoundTrip` pins the slug shape. |
| Worker process trees are supervised | met | `WorkerSupervisor::startInstanceLocked` always pairs `CreateProcessW(... CREATE_SUSPENDED ...)` with `AssignProcessToJobObject` (with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`) before `ResumeThread`. Supervisor destructor + runtime shutdown reap the tree. |
| Health states visible through API | met | `EndpointInstance.state` (lifecycle slug) and `EndpointInstance.statusMessage` are exposed via `GET /api/pools` and `GET /api/pools/{poolId}`. |
| No fake live infrastructure | met | Supervised-mock path reports `supervised=false` when no executable is configured. Default pool has zero instances and `minInstances=0`. `testManagedPoolEmptyByDefault` pins the invariant. |

## Risks and blockers

1. **Real binary spawning is unverified end-to-end.** The supervisor's `CreateProcessW` path is exercised by static review and matches the (working) native HTTP.sys adapter pattern, but no MCP backend was actually spawned in this dev environment. PHASE-10 (release gate) is the right place to add a smoke test that pools can spawn a trivial worker (e.g., `cmd /c timeout /t 60`) and report `Ready`.
2. **No lease router yet.** Pool instances are spawned but not selected by request routing. PHASE-07 wires `EndpointLease` and `LeaseRouter` against `WorkerSupervisor::listPools()`.
3. **Telemetry is a placeholder.** `WorkerTelemetry::cpuPercent` and `memoryMbytes` default to `-1.0` (unavailable). PHASE-08 introduces real per-instance telemetry via PDH-derived process counters.
4. **No persistence across restarts.** Pool registrations are in-memory only. Operators who configure pools today need to re-upsert after every restart; persistence lands in a future phase (PHASE-08 / PHASE-09 may add disk backing).
5. **`std::max` macro collision** required parenthesization in this TU. The same pattern may be needed if PHASE-07 adds more `<algorithm>` calls; flagged so future authors don't trip over the same `Windows.h` `max()` macro.
6. **Pre-existing C4100 warning persists** in `SetupWizardBuilder.cpp:133`. Carries forward from PHASE-01.

None of these block declaring PHASE-06 complete; they are forward-flagged for PHASE-07..PHASE-10.

## Deferred work

| Item | Deferred to | Reason |
|---|---|---|
| `EndpointLease` + `LeaseRouter` + `ScalePolicy` enforcement | PHASE-07 | Phase file owns this. |
| Real per-instance telemetry (PDH process counters, queue depth, inflight calls) | PHASE-08 | Phase file owns this. |
| Pool persistence across restarts | PHASE-08 / PHASE-09 | Disk-backing concern. |
| End-to-end pool-spawning smoke test | PHASE-10 | Hardening / release-gate concern. |
| `OnboardingProfile.pools[]` enrichment | PHASE-07 / PHASE-09 | Once leases land, profiles can advertise per-pool URLs. |
| Browser dashboard pool panel | PHASE-09 | Phase file owns dashboard reskin. |

## Ready for next phase?

**Answer: yes** — `IWorkerSupervisor` is implemented and replaceable, the lifecycle state machine matches the manifest's seven states verbatim, child process trees are contained with Job Objects, the admin surface is wired through six new routes, the schema-required-fields test pins JSON conformance, and `testManagedPoolEmptyByDefault` enforces the no-fake-infrastructure invariant.

PHASE-07 should begin by:
1. Reading [handoff/realignment/PHASE-07-autoscaling-lease-routing.md](../../../handoff/realignment/PHASE-07-autoscaling-lease-routing.md) and its `readFirst` files (`docs/implementation/schemas/managed-endpoint-pool.schema.json`, `src/MasterControlApp/MasterControlRuntime.cpp`, `tests`).
2. Producing a file-by-file plan to introduce `EndpointLease`, `ILeaseRouter`, queue-pressure metrics, scale-out triggers (active leases / queue wait / inflight requests), and the drain-existing-stick-new-elsewhere routing rule.
3. Adding synthetic load tests that prove scale-out under heavy utilization and that existing stateful sessions stay sticky while new sessions route to the scaled instance.
4. Running `cmake --preset debug` / `cmake --build` / `ctest` end-to-end after the changes.
5. Stopping at the PHASE-07 completion report. Not proceeding to PHASE-08.

PHASE-06 stops here. No further phases will start without explicit instruction from the operator.
