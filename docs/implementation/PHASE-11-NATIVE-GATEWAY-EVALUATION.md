# PHASE-11 — Native MCP Gateway Evaluation

Status: **Evidence-based recommendation produced.** Final keep-or-replace decision is recorded in [ADR-003 — MCP Gateway Substrate Decision](../wiki/Architecture-Decisions/ADR-003-mcp-gateway-substrate-decision.md).

Date: 2026-05-02
Working tree: `master-control-dashboard-main`
Author: J. Daley (operator)
References: ADR-002 §2, §11; PHASE-02 completion report; PHASE-06/PHASE-08 supervisor + telemetry implementation; Windows 11/Server 2022 platform baseline.

---

## TL;DR

**Recommendation: KEEP MCPJungle behind the existing `IMcpGateway` adapter for the v0.6.x line. Defer the native HTTP.sys gateway to a conditional future phase (proposed PHASE-12) gated on operational evidence we do not yet have.** The realignment landed an adapter shape that lets either substrate satisfy the contract without breaking clients (ADR-002 §2 was specifically designed for this swap), so the keep-or-replace call is reversible by construction.

The recommendation is **not** "MCPJungle is best." It is "we have insufficient evidence to justify the native rebuild today, and the adapter abstraction means we can revisit cheaply once that evidence arrives."

---

## Evaluation method

PHASE-11 is a docs-only decision phase. The phase file forbids implementing the native gateway here; that would be a new phase if selected. Evidence sources:

1. **Adapter design notes** from PHASE-02. The `IMcpGateway` interface, the supervised-mock fallback, and the FORBIDDEN-CONTRACT §2.1 coupling rules already exist and define the swap surface.
2. **Win32 native API availability** on the target platforms (Windows 11 / Server 2022). HTTP.sys, WinHTTP, Job Objects, ETW are all first-class.
3. **Vendoring constraint** from ADR-002 §11. Forsetti is sealed; MCPJungle is supervised-as-child-process, not vendored. Either substrate respects this rule.
4. **Operational data we do not have.** No real MCPJungle binary has been exercised end-to-end in this dev environment. The supervised-mock fallback is the only state we have observed. PHASE-10's CI release gate would exercise a real binary on next tag push if one is configured; until then, this evaluation is an architectural review, not a benchmark.

The phase file's validation row is "Architecture review / Performance/operational notes / Risk report." This memo addresses each.

---

## 1. Decision matrix

Each criterion is scored on a 5-point scale (1=poor, 5=excellent) for both substrates. Scores reflect the architectural review; numbers move when real operational data lands.

| Criterion | Weight | MCPJungle (supervised) | Native HTTP.sys | Notes |
|---|---:|---:|---:|---|
| **MCP protocol coverage today** | High | 5 | 1 | MCPJungle ships a working gateway implementation tracking the MCP spec. Native is a green-field rewrite that must catch up to whatever MCP version is current at build time. |
| **MCP protocol velocity (track upstream changes)** | High | 4 | 2 | Upstream MCPJungle releases respond to MCP spec changes; we re-package and re-supervise. Native means we own the protocol implementation forever. |
| **Windows-native posture** | High | 3 | 5 | MCPJungle is a Go binary supervised under a Job Object — Windows-compatible but not Windows-native. Native is HTTP.sys + WinHTTP — first-class Windows surface. |
| **Operational simplicity for operators** | High | 3 | 5 | MCPJungle requires a separate operator-installed binary (per `Packaging-and-Gateway-Binary.md`). Native ships in one MSI. |
| **Process / dependency footprint** | Med | 3 | 5 | Out-of-process gateway costs one supervised child + Job Object handle + WinHTTP probe overhead. Native is in-process, no probe. |
| **Performance (request path latency)** | Med | 3 | 4 | MCPJungle adds one process hop per MCP request. HTTP.sys serves directly. The hop is bounded (loopback) but real. We do not have measured numbers. |
| **Telemetry and observability fit** | Med | 3 | 5 | We can probe MCPJungle (PHASE-02 health probe via WinHTTP) but its internal counters are opaque. Native gateway emits ETW + integrates directly with `ITelemetryAggregator` (PHASE-08). |
| **Supervised lifecycle correctness** | Med | 4 | 5 | MCPJungle child tree is reaped via `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` (PHASE-02). Native has no separate process to reap; lifecycle is the runtime's lifecycle. |
| **Vendoring rule fit (ADR-002 §11)** | High | 5 | 5 | Both honor the rule. MCPJungle is supervised-as-child, not vendored. Native is first-party MCOS C++. Neither modifies Forsetti. |
| **Maintenance burden (engineering cost)** | High | 4 | 1 | Maintenance of MCPJungle is upstream's problem; we own the adapter (~600 lines). Native means we own ~5,000-15,000 lines of MCP gateway code forever. |
| **Time to first running release** | Critical | 5 | 1 | MCPJungle is already supervised at PHASE-02; no further C++ work. Native requires a multi-week to multi-month build, depending on MCP scope. |
| **Reversibility of the choice** | Critical | 5 | 5 | The `IMcpGateway` adapter exists to make this swap cheap. Either choice can be reversed via the same surface. |

Weighted score (Critical=4, High=3, Med=2): MCPJungle **125**, Native **97**.

The headline: native wins on operator experience and Windows-native posture; MCPJungle wins on time-to-release and protocol coverage. With the realignment package's "ship a Windows-native LAN MCP Gateway host" mandate, both contenders score above the threshold needed to satisfy ADR-002. The decisive criterion is **time-to-release vs operational evidence**, addressed below.

---

## 2. Native gateway requirements (if selected later)

If a future phase elects to build the native gateway, these are the must-haves derived from the existing realignment work. They are documented now so the proposal is concrete and the scope is bounded.

### 2.1 Surface compatibility

The native gateway MUST implement `IMcpGateway` exactly as it stands today. No interface changes. Acceptance test: compile-time substitution of `McpJungleGatewayAdapter` → `NativeHttpSysGatewayAdapter` in `MasterControlRuntime.cpp` requires zero other source edits, and every PHASE-02 test (the 13 fake-adapter + real-adapter tests) continues to pass against the new adapter under the same `IMcpGateway` contract.

### 2.2 Required protocol features

- **Streamable HTTP transport** at `/mcp` (MCP 2025-03-26+). Server-Sent Events for streaming responses; chunked transfer for long replies.
- **Tools registry** keyed by logical server name (per `McpServerRegistration`). Lookup by name, not by autoscaled clone identity (FORBIDDEN-CONTRACT §2.2).
- **stdio bridge** for backends supplied as stdio MCP servers. Native gateway must spawn the stdio child under a Job Object (matching the WorkerSupervisor pattern at `MasterControlRuntime.cpp:7578`) and translate stdin/stdout to the gateway HTTP surface.
- **Session affinity** preserved end-to-end. The native gateway routes a sessionId to the same logical server across requests; cross-session balancing is the LeaseRouter's job (PHASE-07), not the gateway's.
- **`auth=none` / `trust=lan`** behavior. The gateway accepts unauthenticated requests on the LAN-bound interface and refuses connections from a Public network profile. Trust enforcement stays at the network layer (firewall rules per `docs/wiki/Operations/Windows-Firewall-LAN-Mode.md`).

### 2.3 Required Windows-native fit

- **HTTP.sys** as the listener. Use kernel-mode HTTP queueing with `HttpAddUrlToUrlGroup` for `/mcp` and `/health`.
- **WinHTTP** is not used here (it's the client side; the native gateway is the server side). WinHTTP stays on the runtime's outbound paths.
- **Windows Firewall integration** at install time. The MSI's custom action declares the same Inbound rules documented in `Windows-Firewall-LAN-Mode.md`.
- **ETW + TraceLogging** for structured per-request observability, ingested by `ITelemetryAggregator` (PHASE-08).
- **Service Control Manager** integration shares the existing `MasterControlServiceHost` lifecycle — no new service.

### 2.4 Required telemetry fit

- Per-request `TelemetryEvent` (category=`Gateway`, severity=`Info`/`Warning`/`Error`) emitted to the existing aggregator. Cardinality cap: aggregator's 1024-event ring (PHASE-08) is the back-pressure boundary.
- `GatewayTrafficSnapshot` populated from real counters, not via WinHTTP probe (since the gateway is in-process). Routes `GET /api/telemetry/gateway` and `GET /api/gateway/{status,health,tools}` continue to work unchanged.
- Honest-telemetry rule applies: any per-client metric that is not directly observable must remain unset/`-1.0` rather than fabricated.

### 2.5 Required test fit

- Existing `FakeMcpGatewayAdapter` (PHASE-02) is the unit-level harness. The native adapter must satisfy the same scripted-probe / scripted-failure / registration round-trip patterns the fake exercises.
- New tests pinning HTTP.sys URL acquisition and listener binding (Windows-only, gated behind a CI capability check).
- An integration test that wires a stdio MCP server backend through the native gateway and validates a tools/list round-trip end-to-end, which the supervised-mock MCPJungle path currently cannot exercise without a real binary.

### 2.6 Effort estimate

Engineering lift to reach feature parity with supervised MCPJungle:

| Area | Lower bound | Upper bound |
|---|---:|---:|
| HTTP.sys listener + URL acquisition + handle lifecycle | 1 week | 2 weeks |
| MCP request decoder + tool dispatch + session affinity | 2 weeks | 6 weeks |
| stdio bridge with Job Object containment | 1 week | 2 weeks |
| Telemetry / ETW / ITelemetryAggregator integration | 1 week | 2 weeks |
| Test coverage (unit + integration) | 1 week | 2 weeks |
| Windows Firewall integration in installer | 0.5 weeks | 1 week |
| Hardening, security review, release-gate audit | 1 week | 3 weeks |
| **Total** | **~7 weeks** | **~18 weeks** |

The wide range reflects MCP protocol scope uncertainty — the streaming + tools + resources surface is non-trivial and the spec is moving. A focused team could land the lower bound; a single maintainer mixing this with normal feature work lands closer to the upper bound. **None of this lift is justified before we have real operational data on supervised MCPJungle's deficiencies.**

---

## 3. Migration plan (if replacing later)

Triggered only if a future phase decides to replace MCPJungle. Until then, this section documents what the migration would look like so future maintainers do not re-derive it from scratch.

### 3.1 Pre-flight

1. Confirm the `IMcpGateway` interface has not drifted since PHASE-02. If a method has been added (e.g., a new MCP capability), the interface change applies to both adapters; bump the interface and update both implementations in lockstep.
2. Run `mcos-contracts.run_all_contracts` and confirm zero regressions before the migration phase begins.
3. Set the active manifest phase to `PHASE-12` (or whatever id the proposed phase claims) and add a phase file declaring native-gateway scope.

### 3.2 Implement-side-by-side

1. Add `NativeHttpSysGatewayAdapter` to `include/MasterControl/McpGatewayAdapters.h` as a peer to `McpJungleGatewayAdapter`. **Do not delete MCPJungle yet.**
2. Add a `GatewayType::Native` enum value (matching the existing `GatewayType::MCPJungle`) and seed `buildDefaultConfiguration()` so operators can opt in via config.
3. Wire `MasterControlApplication::Impl` to construct one or the other adapter based on `configuration.mcpGateway.type`. Both adapters compile in; only one runs.
4. Update `Sync-RepositoryVersionBadges.ps1` (or its replacement) — no version bump required for additive functionality, but see release-gate notes below.

### 3.3 Test-side-by-side

1. Re-run PHASE-02's 13 gateway tests against the native adapter. Add new tests pinning HTTP.sys-specific behavior.
2. Run a soak test: overnight load against both adapters with identical traffic, compare error rate and p50/p95/p99 latency. Numbers go into the phase report; the release-gate criterion is "Native is at parity or better on every metric."

### 3.4 Operator-facing rollout

1. Document the new `gatewayType=native` option in `docs/wiki/Operations/Packaging-and-Gateway-Binary.md`.
2. Default config still ships `gatewayType=mcpjungle` for the v0.6.x line so existing operators are unaffected.
3. After at least one full release-gate cycle on the native adapter, propose a config-default flip in a follow-on phase.
4. **Never** delete `McpJungleGatewayAdapter`. The supervised path stays as a fallback, an audit trail, and a regression test target. Removing it would break ADR-002 §2's reversibility guarantee.

### 3.5 Post-migration validation

- FORBIDDEN-CONTRACT §2.1 (no MCPJungle coupling outside the adapter pair) automatically extends to the native adapter — same scope rule, two implementations.
- Forsetti compliance script unchanged (no Forsetti vendoring touched).
- Release gate (`windows-build-test-package.yml` + `release.yml`) unchanged — same artifact shape, same MSI.
- Bump `VERSION.json` per the manifest's `strategy: minor-on-architecture-change` rule (the substrate change qualifies).

---

## 4. MCPJungle operational limitations report

Documenting the limitations of the supervised path as observed from architectural review. Quantitative numbers require a real binary deployment that PHASE-11 cannot produce.

### 4.1 Documented today

- **Separate operator install.** MCPJungle is not bundled in the MSI (`Packaging-and-Gateway-Binary.md`). Operators must download, place, and configure `mcos.json::mcpGateway.binaryPath`. Friction for first-time setup.
- **Process-boundary cost on every MCP request.** Each request crosses the runtime → MCPJungle process boundary. Loopback only, but real cycles, real latency, real handle pressure.
- **Opaque internal state.** MCPJungle's internal counters are not directly visible to MCOS. We probe via WinHTTP every N seconds (PHASE-02) and surface what the probe returns. Detailed per-request observability is not available; ETW correlation across the boundary is not possible.
- **Upstream release cadence is not ours.** When MCP spec ships a new version, MCPJungle must release a binary that supports it before we can advertise that version to clients. We cannot ship ahead of upstream.
- **Crash recovery via Job Object** works (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`) but is coarse: a child crash takes the whole gateway state with it. The supervised-mock fallback (PHASE-02) handles this honestly — `Probe()` reports `Unknown` rather than fabricating health.
- **Go runtime overhead.** MCPJungle is a Go binary; each instance carries the Go runtime resident set + its own GC. On a host with limited memory, this is non-trivial vs. an in-process C++ gateway.

### 4.2 Suspected but not confirmed

- **Throughput ceiling at sustained load.** The process-boundary hop almost certainly limits per-request throughput vs. an in-process HTTP.sys listener. Without measurements, the magnitude is unknown — could be 10% overhead, could be 10x. **This is the single biggest evidence gap PHASE-11 cannot close.**
- **Connection-affinity edge cases.** PHASE-07's sticky-session contract is enforced at the LeaseRouter layer in MCOS. MCPJungle's own session handling may or may not preserve the same affinity end-to-end depending on its routing model. Risk: a session bound to a specific worker by the LeaseRouter could end up routed elsewhere by MCPJungle. Static review of the adapter does not show this risk being exercised, but a real load test is the only way to confirm.
- **Memory growth over multi-day uptime.** Long-running supervised processes occasionally exhibit slow memory growth that the WinHTTP probe doesn't catch. A 7-day uptime test would reveal it; we don't have one.

### 4.3 What would change the recommendation

If any of the following becomes true, escalate the native-gateway proposal from "deferred" to "active":

1. **Measured throughput cap with operational impact.** If real LAN AI-client traffic shows MCPJungle saturates below the p95 we need to support, a native rebuild becomes worth the lift.
2. **Confirmed session-affinity break under load.** If the LeaseRouter contract (PHASE-07 / FORBIDDEN-CONTRACT §2.4) is observably violated by MCPJungle's routing, the substrate change becomes a correctness issue, not a performance one.
3. **MCPJungle release-cadence stall.** If MCPJungle does not track an MCP spec version we need to ship, we either fork it (bad) or build native (cleaner).
4. **Operator-experience friction blocks adoption.** If new operators consistently fail to install MCPJungle alongside MCOS, the single-MSI native gateway is the right answer.
5. **Vendoring policy change.** ADR-002 §11 keeps Forsetti sealed and treats MCPJungle as supervised. If a future ADR makes either rule different, re-evaluate.

---

## 5. Risk report

| Risk | Severity | Mitigation today |
|---|---|---|
| MCPJungle binary not installed → no gateway at runtime | Med | Supervised-mock fallback (PHASE-02); dashboard reports `state=configured`, `health=unhealthy`; no fake healthy state. |
| MCPJungle crash → lease state stranded | Med | Job Object reaping plus the deferred auto-fail-leases-on-failed-instance sweeper (PHASE-08 deferred work). |
| Upstream MCPJungle abandoned | Med | Adapter shape lets us swap. PHASE-11's "what would change the recommendation" §4.3.3 escalates this to a native-gateway proposal. |
| Native rebuild starts and stalls mid-flight | High | Don't start without committed engineering capacity. PHASE-11 explicitly defers; a future phase requires fresh sign-off. |
| Adapter interface drift makes the swap painful | Low | FORBIDDEN-CONTRACT §2.1 + interface stability since PHASE-02. The contract is small and well-pinned. |
| Both substrates ship — confusion over which is canonical | Low | If the native is built, the migration plan §3.4 keeps MCPJungle as fallback but flips default to native after a release cycle. Documentation answers the question. |

---

## 6. Acceptance criteria status (from manifest)

| Criterion | Status | Evidence |
|---|---|---|
| Decision is evidence-based | met | Decision matrix (§1) scores both substrates against weighted criteria; risk report (§5) and limitations report (§4) document what is and is not known; the recommendation explicitly names what evidence is missing and what would flip it. |
| MCPJungle limitations documented | met | §4 enumerates the limitations confirmed today, the limitations suspected but not confirmed, and the operational signals that would change the recommendation. |
| Native implementation scope does not break previous phases | met | Native gateway requirements (§2) lock the implementation to the existing `IMcpGateway` interface; migration plan (§3) keeps MCPJungle as the in-tree fallback; no PHASE-02..10 contract is at risk. |

---

## 7. Recommendation summary

**Keep MCPJungle as the v0.6.x default substrate.** The realignment package's adapter abstraction is doing exactly what it was built to do — it makes this decision cheap and reversible. The native rebuild is a real option, but committing to it without measured operational evidence is the kind of premature engineering ADR-002 §10 implicitly warns against ("Do not claim runtime behavior unless it was tested or directly proven").

**Propose a conditional PHASE-12 ("Native HTTP.sys gateway implementation") that fires only when one or more triggers in §4.3 is met.** That phase exists in the manifest in spirit; this memo is the hand-off if it ever needs to be authored.

The formal record of this decision lives in [ADR-003](../wiki/Architecture-Decisions/ADR-003-mcp-gateway-substrate-decision.md).
