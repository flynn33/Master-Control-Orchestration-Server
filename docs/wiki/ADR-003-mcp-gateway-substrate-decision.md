# ADR-003 - MCP Gateway Substrate: Native HTTP.sys Only (v0.9.0+, status-updated)

- Status: Accepted (status updated 2026-05-11 — supersedes the v0.7.0 two-substrate state)
- Original date: 2026-05-02
- v0.7.0 status update date: 2026-05-05 (both substrates shipped; operator-selectable)
- v0.9.0 status update date: 2026-05-07 (legacy external gateway retired; the in-process HTTP.sys substrate is the only shipping option)
- Deciders: Product owner, engineering
- Builds on: [ADR-002 - Gateway-first MCP realignment](ADR-002-gateway-first-mcp-realignment.md) §2 (replaceable gateway adapter), §11 (vendoring rules)
- Related: [docs/implementation/PHASE-11-NATIVE-GATEWAY-EVALUATION.md](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/docs/implementation/PHASE-11-NATIVE-GATEWAY-EVALUATION.md), [handoff/realignment/PHASE-11-native-gateway-option.md](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/handoff/realignment/PHASE-11-native-gateway-option.md), [handoff/realignment/PHASE-12-native-http-sys-gateway.md](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/handoff/realignment/PHASE-12-native-http-sys-gateway.md)

### Status update (v0.9.0) — Legacy external gateway retired

The v0.7.0 status update (immediately below) recorded that **both substrates ship and are operator-selectable** via `mcpGateway.type`. That intermediate state held for the v0.7.x and v0.8.x lines.

In v0.9.0 the operator directive was: **"MCP Jungle support is to be dropped in place of a custom solution."** The `NativeHttpSysGatewayAdapter` and its supervised-binary management code path were removed. the legacy `GatewayType` slot enum value is retained ONLY so existing v0.6.x..v0.8.x on-disk configs deserialize without rejection — at runtime the type field is ignored and `NativeHttpSysGatewayAdapter` is always instantiated.

What v0.9.0+ deliver:

- `NativeHttpSysGatewayAdapter` on `0.0.0.0:cfg.mcpGateway.listenPort` (default `8080`) at `cfg.mcpGateway.mcpPath` (default `/mcp`).
- `cfg.mcpGateway.type` field retained in JSON schema; runtime value is ignored.
- `cfg.mcpGateway.binaryPath` and `cfg.mcpGateway.databasePath` retained in JSON schema but unused.
- `gatewayConfig.type` is logged but does not affect adapter selection.
- The `IMcpGateway` interface, `FakeMcpGatewayAdapter` (test harness), and every PHASE-03 through PHASE-12 surface remain exactly as they shipped — additive removal only.

The v0.7.0 two-substrate state and the original v0.6.x single-substrate state follow below for historical record.

### Status update (v0.6.9 / v0.6.10 / v0.7.0)

The original ADR-003 decision (recorded below for the historical record) was to **keep the external supervised substrate for the v0.6.x line** and defer a native HTTP.sys-backed gateway to a conditional PHASE-12 gated on five operational triggers. That decision held through v0.6.0 through v0.6.8.

In v0.6.9, **PHASE-12 was authored and shipped voluntarily**, not because any of the five named triggers fired in measurement, but because the operator-experience trigger (#4 in the original list) became visible during day-to-day use: the supervised-binary path required a separate download and configure step that was real friction for fresh installs. The conservative path remains valid; the additional substrate is purely additive.

What v0.6.9 / v0.6.10 / v0.7.0 actually deliver:

- v0.6.9 — `NativeHttpSysGatewayAdapter` ships alongside `NativeHttpSysGatewayAdapter`. Both satisfy `IMcpGateway` exactly. HTTP.sys lifecycle, MCP `initialize` and `tools/list` end-to-end. `tools/call` returned an honest "stdio bridge pending" -32601 with explicit pointer at v0.6.10.
- v0.6.10 — stdio bridge complete. `IWorkerSupervisor::sendStdioJsonRpc` writes a `\n`-terminated JSON-RPC envelope to a supervised child's stdin and reads stdout via `PeekNamedPipe` + deadline-based `ReadFile`, parsing newline-delimited JSON and matching by `id`. Per-instance mutex serializes concurrent calls. Native `tools/list` aggregates by walking each pool's first Ready instance via the bridge; native `tools/call` resolves `params.name` to a pool, acquires a lease, forwards via the bridge, re-stamps the response id. Bootstrapper installs URL ACL `http://+:<port>/ user=Everyone` so console-mode operators bind without elevation.
- v0.7.0 — production milestone. Both substrates ship; operators select via `mcpGateway.type`. The `IMcpGateway` interface is unchanged.

What this status update changes about the original decision:

- The five operational triggers are no longer "deferred". Trigger #4 (operator-experience friction) was acted on. Triggers #1, #2, #3, #5 remain unmeasured but moot now that the native substrate exists.
- "Keep the external supervised substrate for v0.6.x" → "Both substrates ship; operators select." The ADR-002 §2 reversibility guarantee is satisfied in both directions.
- The `IMcpGateway` interface, `NativeHttpSysGatewayAdapter`, `FakeMcpGatewayAdapter`, the gateway HTTP routes (`/api/gateway/*`), and the dashboard panels all remain exactly as they shipped through PHASE-10 — additive only.
- Default for fresh installs as of v0.7.0 is encouraged to be `mcpGateway.type = "native"` because there is no separate binary to install. Existing v0.6.x deployments stay on the legacy external substrate unless they choose to switch.

The original decision text follows below for historical record.

---

### Context

ADR-002 §2 chose an external supervised binary as the first MCP gateway substrate behind a replaceable `IMcpGateway` adapter. ADR-002 §11 also forbade vendoring third-party code into MCOS, so the external substrate was integrated as a supervised child process (PHASE-02), never as in-tree source. The realignment manifest reserved PHASE-11 for the question this ADR answers: **after the spike (PHASE-02) and the operational layers built on top of it (PHASE-03 through PHASE-10), should MCOS keep the external supervised substrate, or replace it with a native HTTP.sys-backed gateway implemented inside MCOS?**

Two facts shape the decision:

1. **The adapter abstraction is doing its job.** PHASE-02 landed `IMcpGateway` plus `NativeHttpSysGatewayAdapter` plus `FakeMcpGatewayAdapter`. PHASE-06 added the worker pool layer that registers logical pools with whatever adapter is in place. PHASE-08 added the telemetry surface that probes the adapter without depending on which substrate it wraps. PHASE-09 wired the dashboard to those probes. Every phase since PHASE-02 has been substrate-agnostic by construction. Either substrate can satisfy the entire stack.
2. **Operational evidence we would need to justify a native rebuild does not exist.** The dev environment never spawned a real gateway binary (the supervised-mock fallback handled its absence honestly). We have architectural review, not measurements. The honest-evidence rule from ADR-002 §10 ("Do not claim runtime behavior unless it was tested or directly proven") cuts both ways — it forbids claiming the external substrate is good enough *and* forbids claiming the native rebuild is necessary. What we have is a small, well-pinned adapter layer and an outstanding question.

The full evaluation memo (`docs/implementation/PHASE-11-NATIVE-GATEWAY-EVALUATION.md`) carries the decision matrix, native gateway requirements, migration plan, operational limitations report, and risk assessment. This ADR records the conclusion in a discoverable place.

### Decision

**Keep the external supervised substrate as the v0.6.x default.** The `IMcpGateway` adapter shape is preserved, no source code changes, no version bump, no client contract changes.

**Defer the native HTTP.sys substrate to a conditional future phase (proposed PHASE-12) gated on operational triggers.** PHASE-12 is not authored or scheduled today. It is documented as a hand-off so a future maintainer who sees one of the trigger conditions has a starting point.

The triggers that would activate PHASE-12, in order of likelihood:

1. **Measured throughput cap with operational impact.** Real LAN AI-client traffic shows the external substrate saturating below the p95 we need to support.
2. **Confirmed session-affinity break under load.** PHASE-07's sticky-session contract (FORBIDDEN-CONTRACT §2.4) is observably violated by the external substrate's routing.
3. **External-substrate release-cadence stall.** Upstream does not track an MCP spec version we need to ship.
4. **Operator-experience friction blocks adoption.** New operators consistently fail to install the external gateway alongside MCOS.
5. **Vendoring policy change.** A future ADR makes either ADR-002 §11 rule different.

If any trigger is observed, the next maintainer runs the migration plan in `docs/implementation/PHASE-11-NATIVE-GATEWAY-EVALUATION.md` §3 and authors PHASE-12.

### What this ADR does NOT change

- ADR-002 §2 remains the canonical statement of the gateway-first model. The `IMcpGateway` interface is locked; both substrates must satisfy it.
- ADR-002 §11 remains in force. Forsetti is sealed; the external substrate is supervised, not vendored. Any future native gateway is first-party MCOS C++ that respects the same vendoring rule.
- The PHASE-02 honest-fallback rule stays in force: the supervised-mock mode reports `state=configured`, `health=unhealthy`, `message="No gateway binary configured."` and the dashboard renders that honestly. No fake live infrastructure (ADR-002 §9).
- PHASE-10's release gate stays in force. `windows-build-test-package.yml` remains the same-SHA product gate; the removed `release.yml` is not part of the current alpha release path. Whichever substrate is configured is exercised by the product gate.

### What this ADR does explicitly change

- The keep-or-replace question is **decided** rather than open. Future sessions and operators do not need to re-derive it from PHASE-02 + PHASE-08 evidence.
- The native gateway is no longer "possible" — it is "conditional, with documented triggers, requirements, and migration plan."
- The MCOS feature roadmap can plan around gateway adapter being the gateway substrate for the v0.6.x line, with a defined escape hatch.

### Consequences

Positive:

- No engineering capacity is consumed on speculative native-gateway work. The seven-to-eighteen-week effort estimate (PHASE-11 evaluation §2.6) goes to other features.
- The realignment package's "ship a Windows-native LAN MCP Gateway host" mandate is met today. No further substrate work is on the critical path.
- The decision is reversible. The adapter abstraction was specifically designed for this. If a trigger fires, the migration plan is already written.

Negative:

- The Windows-native posture of the product is partially compromised. the external substrate is a Go binary supervised by Job Object — Windows-compatible but not Windows-native. The marketing claim "Windows-native" is true of MCOS itself; it is *not* true of the gateway substrate today.
- Operator first-time-install friction persists. the external substrate is a separate download and configure step.
- Per-MCP-request latency carries a process-boundary hop. We do not know the magnitude. This is the largest evidence gap.
- A future PHASE-12 carries non-trivial scope. The lift estimate is documented; the engineering owner is not.

Neutral:

- Either outcome of a future PHASE-12 (keep-external or build-native) stays inside the `IMcpGateway` adapter. ADR-002 §2's reversibility guarantee holds in both directions.

### Implementation note

This ADR is docs-only. No runtime, no test, no schema, no contract changes. The `IMcpGateway` interface, `NativeHttpSysGatewayAdapter`, `FakeMcpGatewayAdapter`, the gateway HTTP routes (`/api/gateway/*`), and the dashboard panels all remain exactly as they shipped through PHASE-10. The PHASE-11 completion report enumerates files changed (only this ADR, the evaluation memo, the drift inventory row, and the phase report itself).

### References

- [ADR-002 §2](ADR-002-gateway-first-mcp-realignment.md) — gateway-first decision
- [ADR-002 §11](ADR-002-gateway-first-mcp-realignment.md) — vendoring rules
- [docs/implementation/PHASE-11-NATIVE-GATEWAY-EVALUATION.md](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/implementation/PHASE-11-NATIVE-GATEWAY-EVALUATION.md) — full evaluation memo
- [handoff/realignment/PHASE-11-native-gateway-option.md](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/../handoff/realignment/PHASE-11-native-gateway-option.md) — phase file
- [handoff/realignment/PHASE-02-completion-report.md](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/docs/archive/realignment-release-reports/PHASE-02-completion-report.md) — adapter spike
- [Packaging-and-Gateway-Binary](Packaging-and-Gateway-Binary) — operator guidance for the supervised path
- [Gateway](Gateway) — current substrate behavior, native HTTP.sys configuration, stdio bridge
