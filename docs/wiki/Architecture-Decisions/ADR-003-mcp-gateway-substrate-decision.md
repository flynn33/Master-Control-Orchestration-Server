## ADR-003 - MCP Gateway Substrate: Keep MCPJungle for v0.6.x

- Status: Accepted
- Date: 2026-05-02
- Deciders: Product owner, engineering
- Builds on: [ADR-002 - Gateway-first MCP realignment](ADR-002-gateway-first-mcp-realignment.md) §2 (replaceable gateway adapter), §11 (vendoring rules)
- Related: [docs/implementation/PHASE-11-NATIVE-GATEWAY-EVALUATION.md](../../implementation/PHASE-11-NATIVE-GATEWAY-EVALUATION.md), [handoff/realignment/PHASE-11-native-gateway-option.md](../../../handoff/realignment/PHASE-11-native-gateway-option.md), [handoff/realignment/PHASE-11-completion-report.md](../../../handoff/realignment/PHASE-11-completion-report.md), [handoff/realignment/PHASE-02-completion-report.md](../../../handoff/realignment/PHASE-02-completion-report.md)

### Context

ADR-002 §2 chose MCPJungle as the first MCP gateway substrate behind a replaceable `IMcpGateway` adapter. ADR-002 §11 also forbade vendoring third-party code into MCOS, so MCPJungle was integrated as a supervised external child process (PHASE-02), never as in-tree source. The realignment manifest reserved PHASE-11 for the question this ADR answers: **after the spike (PHASE-02) and the operational layers built on top of it (PHASE-03 through PHASE-10), should MCOS keep MCPJungle, or replace it with a native HTTP.sys-backed gateway implemented inside MCOS?**

Two facts shape the decision:

1. **The adapter abstraction is doing its job.** PHASE-02 landed `IMcpGateway` plus `McpJungleGatewayAdapter` plus `FakeMcpGatewayAdapter`. PHASE-06 added the worker pool layer that registers logical pools with whatever adapter is in place. PHASE-08 added the telemetry surface that probes the adapter without depending on which substrate it wraps. PHASE-09 wired the dashboard to those probes. Every phase since PHASE-02 has been substrate-agnostic by construction. Either substrate can satisfy the entire stack.
2. **Operational evidence we would need to justify a native rebuild does not exist.** The dev environment never spawned a real MCPJungle binary (the supervised-mock fallback handled its absence honestly). We have architectural review, not measurements. The honest-evidence rule from ADR-002 §10 ("Do not claim runtime behavior unless it was tested or directly proven") cuts both ways — it forbids claiming MCPJungle is good enough *and* forbids claiming the native rebuild is necessary. What we have is a small, well-pinned adapter layer and an outstanding question.

The full evaluation memo (`docs/implementation/PHASE-11-NATIVE-GATEWAY-EVALUATION.md`) carries the decision matrix, native gateway requirements, migration plan, operational limitations report, and risk assessment. This ADR records the conclusion in a discoverable place.

### Decision

**Keep MCPJungle as the v0.6.x default gateway substrate.** The `IMcpGateway` adapter shape is preserved, no source code changes, no version bump, no client contract changes.

**Defer the native HTTP.sys gateway to a conditional future phase (proposed PHASE-12) gated on operational triggers.** PHASE-12 is not authored or scheduled today. It is documented as a hand-off so a future maintainer who sees one of the trigger conditions has a starting point.

The triggers that would activate PHASE-12, in order of likelihood:

1. **Measured throughput cap with operational impact.** Real LAN AI-client traffic shows MCPJungle saturating below the p95 we need to support.
2. **Confirmed session-affinity break under load.** PHASE-07's sticky-session contract (FORBIDDEN-CONTRACT §2.4) is observably violated by MCPJungle's routing.
3. **MCPJungle release-cadence stall.** Upstream does not track an MCP spec version we need to ship.
4. **Operator-experience friction blocks adoption.** New operators consistently fail to install MCPJungle alongside MCOS.
5. **Vendoring policy change.** A future ADR makes either ADR-002 §11 rule different.

If any trigger is observed, the next maintainer runs the migration plan in `docs/implementation/PHASE-11-NATIVE-GATEWAY-EVALUATION.md` §3 and authors PHASE-12.

### What this ADR does NOT change

- ADR-002 §2 remains the canonical statement of the gateway-first model. The `IMcpGateway` interface is locked; both substrates must satisfy it.
- ADR-002 §11 remains in force. Forsetti is sealed; MCPJungle is supervised, not vendored. Any future native gateway is first-party MCOS C++ that respects the same vendoring rule.
- The PHASE-02 honest-fallback rule stays in force: the supervised-mock mode reports `state=configured`, `health=unhealthy`, `message="No gateway binary configured."` and the dashboard renders that honestly. No fake live infrastructure (ADR-002 §9).
- PHASE-10's release gate stays in force. The CI pair (`windows-build-test-package.yml` + `release.yml`) does not change shape; whichever substrate is configured is exercised by the gate.

### What this ADR does explicitly change

- The keep-or-replace question is **decided** rather than open. Future sessions and operators do not need to re-derive it from PHASE-02 + PHASE-08 evidence.
- The native gateway is no longer "possible" — it is "conditional, with documented triggers, requirements, and migration plan."
- The MCOS feature roadmap can plan around supervised MCPJungle being the gateway substrate for the v0.6.x line, with a defined escape hatch.

### Consequences

Positive:

- No engineering capacity is consumed on speculative native-gateway work. The seven-to-eighteen-week effort estimate (PHASE-11 evaluation §2.6) goes to other features.
- The realignment package's "ship a Windows-native LAN MCP Gateway host" mandate is met today. No further substrate work is on the critical path.
- The decision is reversible. The adapter abstraction was specifically designed for this. If a trigger fires, the migration plan is already written.

Negative:

- The Windows-native posture of the product is partially compromised. MCPJungle is a Go binary supervised by Job Object — Windows-compatible but not Windows-native. The marketing claim "Windows-native" is true of MCOS itself; it is *not* true of the gateway substrate today.
- Operator first-time-install friction persists. MCPJungle is a separate download and configure step.
- Per-MCP-request latency carries a process-boundary hop. We do not know the magnitude. This is the largest evidence gap.
- A future PHASE-12 carries non-trivial scope. The lift estimate is documented; the engineering owner is not.

Neutral:

- Either outcome of a future PHASE-12 (keep-MCPJungle or build-native) stays inside the `IMcpGateway` adapter. ADR-002 §2's reversibility guarantee holds in both directions.

### Implementation note

This ADR is docs-only. No runtime, no test, no schema, no contract changes. The `IMcpGateway` interface, `McpJungleGatewayAdapter`, `FakeMcpGatewayAdapter`, the gateway HTTP routes (`/api/gateway/*`), and the dashboard panels all remain exactly as they shipped through PHASE-10. The PHASE-11 completion report enumerates files changed (only this ADR, the evaluation memo, the drift inventory row, and the phase report itself).

### References

- [ADR-002 §2](ADR-002-gateway-first-mcp-realignment.md) — gateway-first decision
- [ADR-002 §11](ADR-002-gateway-first-mcp-realignment.md) — vendoring rules
- [docs/implementation/PHASE-11-NATIVE-GATEWAY-EVALUATION.md](../../implementation/PHASE-11-NATIVE-GATEWAY-EVALUATION.md) — full evaluation memo
- [handoff/realignment/PHASE-11-native-gateway-option.md](../../../handoff/realignment/PHASE-11-native-gateway-option.md) — phase file
- [handoff/realignment/PHASE-02-completion-report.md](../../../handoff/realignment/PHASE-02-completion-report.md) — adapter spike
- [docs/wiki/Operations/Packaging-and-Gateway-Binary.md](../Operations/Packaging-and-Gateway-Binary.md) — operator guidance for the supervised path
