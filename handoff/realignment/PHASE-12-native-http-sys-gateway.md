# PHASE-12 — Native HTTP.sys MCP Gateway

## Status

- Decided: yes, conditional on operator-experience friction trigger from ADR-003 §"triggers".
- Scheduled: pending operator priority. Reserved phase-id slot.
- Supersedes: the v0.6.7 honest-503 listener (`NativeHttpSysGatewayAdapter::startHonestUnavailableListenerLocked`) for the in-process portion. native HTTP.sys gateway remains supported via the existing `NativeHttpSysGatewayAdapter` for operators who prefer the supervised-binary path.

## Mission

Replace the gateway adapter child process with a Windows-native MCP Gateway implemented inside MCOS in C++ on top of HTTP.sys. After PHASE-12, operators get a usable gateway out of the box with no external binary to install.

## Why

ADR-003 made native HTTP.sys gateway the v0.6.x default and reserved this phase for the rebuild. Operator-experience-friction trigger #4 has fired ("New operators consistently fail to install native HTTP.sys gateway alongside MCOS"). Building native eliminates the friction completely and aligns the substrate with `.claude/rules/10-windows-native-cpp.md`'s "Windows-native first" rule.

## Non-negotiables

- The new substrate satisfies `IMcpGateway` exactly. No interface changes. PHASE-02 through PHASE-11 are substrate-agnostic by construction; PHASE-12 must keep that property.
- The supervised-mock fallback rule from ADR-002 §9 stays: when the gateway is intentionally disabled (operator-set `mcpGateway.enabled=false`), the runtime reports honest "disabled" state and the v0.6.7 503 listener answers the port. PHASE-12 only runs when `enabled=true`.
- The native HTTP.sys adapter remains shipped and selectable. Operators who already have native HTTP.sys gateway running get to keep it. `mcpGateway.type` switches between `"native HTTP.sys gateway"` (existing adapter) and `"native"` (new adapter).
- ADR-002 §11 vendoring rules: no third-party HTTP server in the C++ tree. HTTP.sys (Win32) only.

## Scope

### In scope (minimum-viable subset)

1. **`NativeHttpSysGatewayAdapter` class** implementing `IMcpGateway`. Drop-in alongside `NativeHttpSysGatewayAdapter`.
2. **HTTP.sys listener** bound to `mcpGateway.listenHost:mcpGateway.listenPort`. URL Group + Server Session, kernel-mode-routed.
3. **Streamable-HTTP MCP transport** decode/encode (the JSON-RPC + SSE shape from the MCP spec).
4. **Path router** mapping `/mcp/{poolId}` and `/agents/{poolId}` to the supervised pools' logical endpoints.
5. **Stdio bridge per instance** — bidirectional pipe between the gateway loop and each pool instance's existing `npx`-spawned child. Multiplexed by `LeaseRouter`-selected `instanceId`.
6. **MCP protocol surface**: `tools/list`, `tools/call`, plus the `notifications/initialized` handshake.
7. **Wired into existing telemetry**: gateway events into the v0.6.8 telemetry events ring; per-request latency into `GatewayTrafficSnapshot`.
8. **Health endpoint** at `mcpGateway.healthPath` (default `/health`) returning structured JSON.
9. **Gateway selection at construction**: MCOS reads `mcpGateway.type`; constructs `NativeHttpSysGatewayAdapter` when `"native"`, falls back to `NativeHttpSysGatewayAdapter` for `"native HTTP.sys gateway"` (default for backward compat).

### Out of scope (later phases)

- Authentication / authorization (LAN-trusted per ADR-001 §3 / ADR-002 §1; PHASE-12 stays auth-none, trust-lan).
- Streamable-HTTP server-sent-events for long-running tool calls (initial cut returns synchronous JSON-RPC responses; SSE is an optimization).
- HTTPS termination (HTTP.sys supports it natively but operator-side TLS configuration is out of scope here).
- gRPC variant of MCP (the spec is HTTP/JSON-RPC + SSE; gRPC isn't authoritative).
- Hot-reload of pool definitions (PHASE-12 reads pools at startup + on supervisor change events; reload-on-config-write is an optimization).

## File-by-file plan

| File | Action | Why |
|---|---|---|
| `include/MasterControl/McpGatewayAdapters.h` | Edit | Declare `NativeHttpSysGatewayAdapter` alongside the existing two adapter classes. Same `IMcpGateway` interface. |
| `src/MasterControlApp/McpGatewayAdapters.cpp` | Edit | Implement `NativeHttpSysGatewayAdapter::Start/Stop/CurrentStatus/Probe/Register*/ListTools` using HTTP.sys + the stdio bridge. Roughly 800-1200 lines for the MVP. |
| `src/MasterControlApp/McpStdioBridge.cpp` | Create | Bidirectional named-pipe ↔ stdio shim. One bridge instance per supervised worker process. |
| `include/MasterControl/McpStdioBridge.h` | Create | Public surface for the bridge: `Bridge::sendRequest(json) -> json`, lease-router-selected instanceId routing. |
| `src/MasterControlApp/McpProtocol.cpp` | Create | MCP JSON-RPC message decode + encode. Validates `method`, dispatches by name. |
| `include/MasterControl/McpProtocol.h` | Create | Public surface: `parseRequest(...)`, `formatResponse(...)`. |
| `src/MasterControlApp/MasterControlRuntime.cpp` | Edit | Adapter-selection logic in the runtime construction site (currently hardcodes `NativeHttpSysGatewayAdapter`). |
| `src/MasterControlApp/CMakeLists.txt` | Edit | Add `httpapi.lib` to the link line for HTTP.sys. |
| `tests/...` | Create | Test-only `FakeNativeGatewayAdapter` mirroring `FakeMcpGatewayAdapter`'s test scripting hooks. Plus end-to-end test: register pool, send `tools/list` via HTTP, verify the supervised child process sees the request and responds. |
| `docs/wiki/Gateway.md` | Edit | New "Native HTTP.sys gateway" section explaining the trade-off matrix vs native HTTP.sys gateway. |
| `docs/wiki/ADR-003-mcp-gateway-substrate-decision.md` | Edit | Append "PHASE-12 landed" section under "What this ADR does NOT change" stating the conditional became unconditional with the operator-friction trigger. |
| `docs/wiki/Configuration.md` | Edit | `mcpGateway.type` field documentation: new enum value `"native"`. |
| `handoff/realignment/PHASE-12-completion-report.md` | Create | Required per `.claude/rules/40-validation-reporting.md`. |

## Acceptance criteria

1. Fresh MCOS install with `mcpGateway.type = "native"` and `mcpGateway.enabled = true`: TCP 8080 listener answers `tools/list` against any registered pool with the expected JSON-RPC envelope.
2. The 7 npx-based pool recipes (`mcp-filesystem`, `mcp-memory`, `mcp-everything`, `mcp-sequential-thinking`, `subagent-coordinator`, `subagent-memorian`, `subagent-thinker`) all reachable from a remote Claude Code instance pointing at `http://192.168.1.7:8080/mcp`.
3. `LeaseRouter` selection and sticky-session contract continue to behave identically to the native HTTP.sys gateway path. PHASE-07 sticky-session test pins this.
4. Switching `mcpGateway.type` between `"native"` and `"native HTTP.sys gateway"` via POST `/api/config` and a service restart fully reconfigures the substrate without code change.
5. Per-request latency at p50 within 2× the synchronous-localhost baseline (HTTP.sys + stdio bridge round-trip). p99 within 5×. (Numbers measured during PHASE-12, not asserted in tests.)
6. `MasterControlOrchestrationServer-vX.Y.Z-win-x64.msi` ships with the native gateway selected by default. Operators who want native HTTP.sys gateway can flip the config field.

## Effort estimate

- MVP (this plan): 1-2 weeks of focused engineering.
- Production-ready (SSE, TLS handoff, multi-tenant LAN auth, hot-reload, full MCP spec coverage): 4-8 weeks.
- The MVP is enough to retire the legacy external-gateway dependency for new installs. Existing native HTTP.sys gateway deployments keep working via the preserved adapter.

## Risks

| Risk | Mitigation |
|---|---|
| HTTP.sys requires URL ACL registration with admin rights on first bind. | Bootstrap script (existing `MasterControlBootstrapper.exe install`) registers the URL ACL during MSI install. |
| Stdio bridge deadlock if a supervised child blocks reading stdin. | Bridge owns separate I/O threads with bounded queues; timeout returns `LeaseState::Failed` honestly. |
| Streamable-HTTP SSE complexity creep on a "minimum viable" plan. | MVP returns synchronous JSON-RPC. SSE is later phase. |
| Operator confusion about `mcpGateway.type`. | Default stays `"native HTTP.sys gateway"` until PHASE-12 ships v1.0; flip default to `"native"` in v0.7.0. |

## Dependencies

- ADR-002 (gateway-first realignment) — locked. Unchanged.
- ADR-003 (substrate decision) — amended on PHASE-12 land.
- v0.6.8's pool persistence (this release) — needed so the gateway can register its endpoints on every startup without operator re-config.
- PHASE-07 lease router contract — unchanged. Native gateway calls into the existing `LeaseRouter` exactly the same way the native HTTP.sys adapter would have.

## Cross-references

- [ADR-003 substrate decision](../../docs/wiki/ADR-003-mcp-gateway-substrate-decision.md)
- [Gateway docs](../../docs/wiki/Gateway.md)
- [v0.6.7 honest-503 listener](../../src/MasterControlApp/McpGatewayAdapters.cpp) — direct precursor; the listener becomes a fallback when `enabled=false`.
- [MCP spec](https://modelcontextprotocol.io) — Streamable-HTTP transport reference.
