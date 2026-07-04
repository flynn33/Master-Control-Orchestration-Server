# ADR-002 - MCOS is a Windows-Native LAN MCP Gateway Host (Gateway-First Realignment)

- Status: Accepted
- Date: 2026-04-30
- Deciders: Product owner, engineering
- Builds on: [ADR-001 - LAN Client Control Plane](ADR-001-lan-client-control-plane.md)
- Supersedes (in part): ADR-001 §3 (per-client `X-MCOS-Client-Id` as the AI-client connection model) and ADR-001 §6 (the catalog read surface as the AI-client tool path)
- Related: [docs/implementation/MCOS-REALIGNMENT-MASTER.md](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/implementation/MCOS-REALIGNMENT-MASTER.md), [docs/implementation/MCP-GATEWAY-DISCOVERY-CONTRACT.md](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/implementation/MCP-GATEWAY-DISCOVERY-CONTRACT.md), [docs/implementation/CLU-GOVERNANCE-BUNDLE-CONTRACT.md](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/implementation/CLU-GOVERNANCE-BUNDLE-CONTRACT.md), [handoff/realignment/manifest.json](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/../handoff/realignment/manifest.json)

## Status update (v0.9.0)

the legacy external gateway was retired at v0.9.0. The shipping substrate is now `NativeHttpSysGatewayAdapter` (in-process HTTP.sys) behind `IMcpGateway`. The decision-point text below (which named the legacy external gateway as the v0.6.x default and reserved PHASE-11 to evaluate replacement) is preserved as historical record; ADR-003 captures the v0.9.0 substrate decision.

---

### Context

ADR-001 corrected MCOS away from embedded provider execution and toward a LAN client control plane. Its provider-removal program (`ProviderIntegrationModule`, vendor modules, `Provider*` / `AutoConnect*` data, `/api/providers/*`, outbound CLI transports) has been delivered in the runtime and modules. Static inspection of `src/MasterControlApp/MasterControlRuntime.cpp` and `src/MasterControlModules/MasterControlModules.cpp` finds zero `Provider*` / `AutoConnect*` / `/api/providers` references; residual references survive only in the WinUI shell (`MainWindow.xaml.cpp`, `ShellRuntime.cpp` lines 1487-1504, 2391, 2405) which is in deferred-cleanup state, and in historical artifacts (`CHANGELOG.md`, `VERSION.json`, `docs/wiki/Versions.md`, `plans/`). That ground is held.

What ADR-001 did **not** answer is how external AI clients actually reach the shared MCP and sub-agent fabric, how that fabric is discovered on the LAN, how shared servers and sub-agents are operated as supervised infrastructure, and how the trust boundary divides the maintainer surface from the AI-client surface. ADR-001's answer was implicit: every authenticated client identifies itself with `X-MCOS-Client-Id`, then reads the catalog from `/api/client/mcp-servers` and `/api/client/sub-agents` and addresses backends individually.

That answer scales poorly. Each client must be configured with the catalog endpoints, each client must implement MCOS-specific identification, and addressing every backend MCP server independently pollutes client tool lists and breaks under autoscaling (every clone would have to register as a separate public tool namespace). The mainstream MCP gateway pattern (a single client-facing endpoint that aggregates many backend servers) is not optional at this point. External AI clients (Claude Code, Codex, Grok, ChatGPT connector-edge, generic MCP clients) expect to be pointed at one URL and to receive aggregated tool listings transparently.

In parallel, the existing repo carries usable pieces of the gateway-first direction — `BeaconGatewayModule.json`, `/api/platform-services/config/{platform}`, the platform-aware bundle export, and `plans/dashboard/remote-client-onboarding.md` — but they are partial and not coherent with each other. The 2026-04-30 Realignment Package consolidates them and adds the missing pieces.

### Decision

MCOS is a Windows-native LAN MCP Gateway host. AI coding clients connect to one MCOS-advertised MCP endpoint and consume an aggregated tool surface; MCOS operates the supervised worker fabric behind that endpoint. The full realignment is captured in `docs/implementation/MCOS-REALIGNMENT-MASTER.md`; this ADR locks the architectural commitments.

Specific consequences locked by this decision:

1. **Two surfaces, one trust model.** The existing admin/maintainer API (`/api/clients/*`, `/api/runtime/*`, `/api/forsetti/*`, `/api/clu/approvals`, browser dashboard) keeps the LAN-trusted/`X-MCOS-Client-Id` model from ADR-001 §3 — maintainers are identified for activity attribution and privileged mutation. The new AI-client surface (the MCP Gateway URL) carries `auth=none`, `trust=lan`, and no app-layer client identity. Trust on the AI-client surface is enforced by the trusted LAN boundary, Windows Firewall scoping, maintainer-controlled LAN-mode enablement, subnet/interface policy, and host/origin validation where compatible. The admin port and the gateway port are logically distinct services.

2. **One MCP Gateway URL.** MCOS advertises exactly one MCP endpoint to AI clients. The first gateway substrate is **an external supervised-binary gateway**, run as a Windows child process and wrapped behind a replaceable C++ adapter `IMcpGateway` / `NativeHttpSysGatewayAdapter`. PHASE-11 evaluates whether to keep the external substrate or replace it with a native gateway built on HTTP.sys/WinHTTP; the adapter exists so that decision does not break clients.

3. **Autoscaled clones are never exposed as separate public tools.** When a managed worker pool scales out, new instances live behind a stable logical pool endpoint registered with the gateway. Clients see one logical tool namespace per pool, never per instance.

4. **LAN discovery is DNS-SD/mDNS first.** MCOS advertises three service types on the local link: `_mcos._tcp.local`, `_mcos-mcp._tcp.local`, `_mcos-onboarding._tcp.local`. TXT fields carry `product=MCOS`, `role=mcp-gateway`, `gateway=native` (legacy external-gateway slug retired v0.9.0), `mcp_path`, `config_path`, `governance_path`, `protovers`, `auth=none`, `trust=lan`, `clu=true`, `forsetti=true`. A normalized discovery document is served at `/.well-known/mcos.json` and mirrored by `/api/discovery`. A UDP JSON beacon remains as a fallback.

5. **Onboarding profiles per client type.** MCOS generates configuration tailored to each known AI client at `/api/onboarding/{clientType}` for `claude-code`, `codex`, `grok`, `chatgpt`, and `generic`. Every profile points at the single gateway MCP URL, declares `authRequired=false`, links to the governance bundle for the requesting platform, and includes config snippets, manual instructions, and verification steps. ChatGPT is documented as a connector-edge case where local LAN connectivity is constrained. The existing `/api/platform-services/config/{platform}` endpoint is the precursor and will be subsumed.

6. **CLU/Forsetti is the governance distributor — not an AI-client auth system.** CLU serves platform-specific governance bundles at `/api/governance/bundles/{windows|macos|ios}`, plus a profile endpoint and a decision endpoint. Each bundle carries `platform`, `forsettiFrameworkVersion`, `agenticCodingFrameworkVersion`, `cluSchemaVersion`, `instructionsMarkdown`, `rulesJson`, `decisionPolicy`, `checksum`, `generatedAt`. The existing CLU profile in `resources/clu/governance-profile.json` and routes `/api/clu`, `/api/client/governance/*` are the precursors. CLU continues to gate maintainer mutations through the maintainer surface; CLU does not gate AI-client tool listing or tool invocation through the gateway.

7. **MCP servers and sub-agents are supervised managed endpoint pools.** Backends become pools of supervised process instances behind a stable logical endpoint. The model is `EndpointTemplate`, `EndpointInstance`, `ManagedEndpointPool`, `WorkerSupervisor`, `EndpointLease`, `LeaseRouter`, `ScalePolicy`, `DrainPolicy`. Worker process trees are contained with Windows Job Objects (`CreateProcessW` + `AssignProcessToJobObject` + `SetInformationJobObject`). Health, readiness, and queue/wait/saturation telemetry are visible through the runtime API and the dashboard.

8. **Autoscaling is sticky-session safe.** When a pool saturates, MCOS scales out the same logical pool. Stateful MCP sessions stay sticky to their existing instance; new sessions/leases route to the new instance. MCOS does not hot-migrate active in-flight stateful streams unless a backend-specific migration contract exists. Drain preserves existing sessions; forced retirement may require client reinitialize.

9. **Telemetry is honest.** Host telemetry uses PDH (CPU/disk/network/process counters) and DXGI (GPU memory where available). Activity uses ETW/TraceLogging where appropriate. Worker telemetry comes from the supervised process tree (CPU/memory/I/O) plus pool metrics (active leases, queue depth, inflight calls). Per-AI-client CPU/GPU/disk telemetry exists only when the client supplies it via heartbeat or sidecar; otherwise the dashboard shows an honest "unavailable" state. No fake utilization, no fake live infrastructure, no invented success states.

10. **Windows-native first.** Service hosting via Windows SCM patterns. HTTP front door via HTTP.sys (or a clearly justified transitional layer). Outbound HTTP via WinHTTP. LAN discovery via Win32 DNS-SD APIs (`DnsServiceRegister`, `DnsServiceBrowse`); Bonjour/mDNSResponder is a deliberate fallback only. No Java or interpreted runtime in core MCOS. Python is permitted for test tooling only. the external substrate runs as a supervised child process behind the C++ adapter. Docker is a development/testing option, not the required production path.

11. **Forsetti vendoring is unchanged.** No edits to `Forsetti-Framework-Windows-main/`. Module manifest boundaries are preserved. `scripts/check-mastercontrol-forsetti.ps1` is updated when architecture changes invalidate its assumptions, not before.

12. **Phased delivery is mandatory.** All work proceeds through the 12-phase manifest at `handoff/realignment/manifest.json` (PHASE-00 through PHASE-11), one phase at a time, file-by-file plan before edits, completion report after edits, no version bump until PHASE-10. PHASE-00 (this baseline) is docs-only.

### What this ADR does NOT change from ADR-001

- ADR-001 §1 (provider stack removal) is reaffirmed. Providers do not return; the residual shell references and historical artifacts noted above are PHASE-01/PHASE-09 cleanup, not architectural reversals.
- ADR-001 §2 (MCOS never calls AI models) is reaffirmed.
- ADR-001 §4 (per-client privilege flags on `LanClient`) survives on the **maintainer surface**. The flags continue to gate maintainer-driven mutations of MCP servers, sub-agents, clients, modules, and governance policy.
- ADR-001 §5 (autonomous mode for MCP/sub-agent creation) survives on the maintainer surface.
- ADR-001 §7 (CLU governance is central) is reaffirmed and extended into platform bundle distribution.

### What this ADR explicitly changes from ADR-001

- ADR-001 §3 (no auth + `X-MCOS-Client-Id` as the AI-client connection model) is **superseded for the AI-client surface**. The AI-client gateway carries `auth=none`, `trust=lan`, and no per-request client identity. ADR-001 §3 stays in force on the maintainer surface for activity attribution.
- ADR-001 §6 (catalog read surface as the AI-client tool path) is **superseded**. AI clients reach tools through the single gateway URL; the existing `/api/client/mcp-servers` and `/api/client/sub-agents` routes remain available to maintainer tooling but are no longer the AI-client path.

### Consequences

Positive. AI clients consume MCOS through the same shape they consume any modern MCP gateway: one URL, aggregated tools, no proprietary auth header. The native HTTP.sys adapter lets MCOS adopt a working substrate quickly and replace it later. Worker supervision via Job Objects prevents orphaned process trees. Autoscaling becomes possible without breaking client-visible tool namespaces. Telemetry honesty closes the gap between dashboard visuals and reality. The trust split (maintainer vs AI-client) keeps the privilege model where it is useful (maintainers) and removes it where it gets in the way (AI clients).

Negative. The repo grows new subsystems (gateway adapter, discovery service, onboarding service, governance bundle service, supervisor, lease router, autoscaler, telemetry aggregator, dashboard reskin, CI hardening). PHASE-09 reskins the dashboard, which means the current Tron UI loses panels that centered on the old per-client/X-MCOS-Client-Id model. The shell's `ProvidersSectionControl` deferred cleanup must finally land in a later phase. The trusted-LAN assumption is even more load-bearing than under ADR-001, because the AI-client gateway carries no app-layer auth at all.

Neutral. the external substrate is a supervised dependency for the duration of PHASE-02 through PHASE-10. PHASE-11 makes the keep-or-replace call based on operational evidence. Either outcome stays inside the `IMcpGateway` adapter; no client contract changes either way.

### References

- Realignment master: [docs/implementation/MCOS-REALIGNMENT-MASTER.md](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/implementation/MCOS-REALIGNMENT-MASTER.md)
- Gateway/discovery contract: [docs/implementation/MCP-GATEWAY-DISCOVERY-CONTRACT.md](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/implementation/MCP-GATEWAY-DISCOVERY-CONTRACT.md)
- CLU governance bundle contract: [docs/implementation/CLU-GOVERNANCE-BUNDLE-CONTRACT.md](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/implementation/CLU-GOVERNANCE-BUNDLE-CONTRACT.md)
- Phase manifest: [handoff/realignment/manifest.json](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/../handoff/realignment/manifest.json)
- Phase index: [handoff/realignment/00-START-HERE.md](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/../handoff/realignment/00-START-HERE.md)
- Provider-era removal map (PHASE-00 baseline): [docs/implementation/PROVIDER-ERA-REMOVAL-MAP.md](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/implementation/PROVIDER-ERA-REMOVAL-MAP.md)
- Architecture drift inventory (PHASE-00 baseline): [docs/implementation/ARCHITECTURE-DRIFT-INVENTORY.md](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/implementation/ARCHITECTURE-DRIFT-INVENTORY.md)
- Forbidden-contract grep list (PHASE-00 baseline): [docs/implementation/FORBIDDEN-CONTRACT-GREP-LIST.md](https://github.com/flynn33/Master-Control-Orchestration-Server/blob/main/implementation/FORBIDDEN-CONTRACT-GREP-LIST.md)
