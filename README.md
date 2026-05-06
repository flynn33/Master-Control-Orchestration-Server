# Master Control Orchestration Server

![version](https://img.shields.io/badge/version-v0.7.1-00f6ff?style=flat-square)
![released](https://img.shields.io/badge/released-2026--05--06-031018?style=flat-square)
![platform](https://img.shields.io/badge/platform-Windows%2011%20%E2%80%A2%20Server%202022-0a1018?style=flat-square)
![toolchain](https://img.shields.io/badge/toolchain-C%2B%2B20%20%E2%80%A2%20WinUI%203%20%E2%80%A2%20CMake-00aacc?style=flat-square)
![architecture](https://img.shields.io/badge/architecture-LAN%20MCP%20Gateway%20Host-1cf2c1?style=flat-square)
![governance](https://img.shields.io/badge/governance-CLU%20%2B%20Forsetti-5a00e8?style=flat-square)
![license](https://img.shields.io/badge/license-Proprietary-031018?style=flat-square)

> **A Windows-native LAN MCP Gateway host.** External AI coding clients (Claude Code, Codex, Grok, ChatGPT, generic MCP) connect to one MCOS-advertised endpoint, consume server-generated onboarding profiles and CLU/Forsetti governance bundles, and operate against supervised MCP server and sub-agent worker pools. MCOS owns discovery, governance, telemetry, worker supervision, autoscaling, dashboarding, and Windows packaging.

---

## The product in one diagram

```mermaid
flowchart LR
    classDef accent fill:#031018,stroke:#00F6FF,color:#E6FCFF,stroke-width:2px;
    classDef faint fill:#0a1018,stroke:#5A00E8,color:#8CB7C4;
    classDef client fill:#031827,stroke:#5AE8FF,color:#A8DCFF;
    classDef good fill:#031a14,stroke:#1cf2c1,color:#a8efe0;

    Operator((👤 Operator))

    subgraph LANClients[LAN AI clients]
        ClaudeCode[/Claude Code/]:::client
        Codex[/Codex/]:::client
        Grok[/Grok/]:::client
        ChatGPT[/ChatGPT connector-edge/]:::client
        Generic[/Generic MCP/]:::client
    end

    subgraph MCOS[Master Control Orchestration Server]
        Discovery[LAN Discovery<br/>DNS-SD + UDP beacon]:::accent
        Gateway[MCP Gateway<br/>MCPJungle OR Native HTTP.sys]:::accent
        Onboarding[Onboarding Profiles<br/>per client type]:::accent
        Governance[Governance Bundles<br/>Windows / macOS / iOS]:::accent
        Supervisor[Worker Supervisor<br/>+ Lease Router + stdio bridge]:::accent
        Telemetry[Telemetry Aggregator]:::accent
        Pools[(Managed Endpoint Pools<br/>MCP servers + sub-agents)]:::good
    end

    Operator -->|Browser dashboard + WinUI shell| Telemetry
    LANClients -->|"DNS-SD discovery (auth=none, trust=lan)"| Discovery
    Discovery --> Gateway
    LANClients -->|MCP requests| Gateway
    Gateway --> Supervisor
    Supervisor --> Pools
    Pools -.->|heartbeats| Telemetry
    LANClients -.->|first connect| Onboarding
    LANClients -.->|on demand| Governance
```

The architecture target is the **gateway-first MCP host** declared in [ADR-002](docs/wiki/ADR-002-gateway-first-mcp-realignment.md) and locked at the substrate level by [ADR-003](docs/wiki/ADR-003-mcp-gateway-substrate-decision.md). As of v0.6.9 / v0.7.0 both substrates ship: the original supervised MCPJungle adapter (the conservative path from PHASE-02) and the in-process Windows-native HTTP.sys adapter (PHASE-12, completed end-to-end in v0.6.10). Operators select via `mcpGateway.type = "mcpjungle" | "native"`. The original [ADR-001 LAN client identity model](docs/wiki/ADR-001-lan-client-control-plane.md) survives as the operator surface that coexists with the AI-client gateway surface.

---

## Quick links

- **Wiki (operator-facing)** → [github.com/flynn33/Master-Control-Orchestration-Server/wiki](https://github.com/flynn33/Master-Control-Orchestration-Server/wiki)
- **Quick Start** → [docs/wiki/Quick-Start.md](docs/wiki/Quick-Start.md)
- **Architecture** → [docs/wiki/Architecture.md](docs/wiki/Architecture.md)
- **Architecture Decisions** → [docs/wiki/Architecture-Decisions.md](docs/wiki/Architecture-Decisions.md)
- **Gateway (substrate selection, install, health probe)** → [docs/wiki/Gateway.md](docs/wiki/Gateway.md)
- **Worker Pools** → [docs/wiki/Worker-Pools.md](docs/wiki/Worker-Pools.md)
- **Onboarding an AI client** → [docs/wiki/Onboarding.md](docs/wiki/Onboarding.md)
- **Versions** → [docs/wiki/Versions.md](docs/wiki/Versions.md)
- **CHANGELOG** → [`CHANGELOG.md`](CHANGELOG.md)
- **VERSION.json** (canonical) → [`VERSION.json`](VERSION.json)

---

## Why MCOS exists

Multiple AI coding clients on the same trusted LAN need to share an MCP server and sub-agent fabric without each client operating in isolation, without each client being hand-configured against every backend, and without one bad client ruining the others' state. MCOS is the Windows-native orchestration plane:

1. **One advertised endpoint.** AI clients on the LAN find MCOS via Bonjour-compatible DNS-SD and connect to a single MCP gateway URL. No per-backend wiring on the client side.
2. **Supervised workers.** MCP servers and sub-agents run as managed pools with a 7-state lifecycle, supervised under Windows Job Objects so MCOS reaps the worker tree atomically on shutdown or crash.
3. **Sticky-session lease routing with same-type scale-out.** The lease router preserves stateful sessions on their original instance, fans new stateless sessions across the least-loaded ready instances, and triggers same-type spawns under saturation.
4. **Honest telemetry.** Every numeric metric uses a `-1.0` "unavailable" sentinel rather than fabricating values. The dashboard renders unreported metrics as `unavailable`, not `0%`.
5. **CLU/Forsetti governance.** Per-platform governance bundles distributed via HTTP. Operator approval queue for high-impact actions.
6. **Reversible by construction.** Every gateway-related decision sits behind the `IMcpGateway` adapter. The MCPJungle substrate is supervised, not vendored; it can be replaced without breaking client contracts.

---

## v0.7.0 — production milestone

**Architecture complete.** Every numbered phase from PHASE-00 (repository baseline + ADR lock) through PHASE-12 follow-up (native HTTP.sys gateway with end-to-end stdio bridge to supervised pool children, shipped in v0.6.10) is delivered, validated, and shipping. The 0.7.0 minor bump under the manifest's `minor-on-architecture-change` policy marks the architectural-completion line: PHASE-12 follow-up was the last architectural change.

Both gateway substrates ship and are operator-selectable via `mcpGateway.type`:

| `type` value | Substrate | When to pick |
|---|---|---|
| `"mcpjungle"` | Supervises an external MCPJungle binary as a Job Object child. v0.6.7 honest-503 listener fills the port when no binary is configured. | Conservative path. Operators with an existing MCPJungle deployment, or who want to track an upstream that may add features faster than MCOS does. |
| `"native"` | In-process Windows-native HTTP.sys server bound directly inside MCOS. No external binary. v0.6.10 stdio bridge forwards `tools/list` and `tools/call` to supervised pool children. | Default for fresh installs. No second binary to manage. URL ACL self-registered by the bootstrapper at install time. |

What v0.7.0 brings together:

- **PHASE-00 through PHASE-11** — gateway-first realignment (ADR-002), worker pool fabric, lease routing, real-time telemetry, Tron dashboard, Windows hardening, native gateway evaluation (ADR-003).
- **PHASE-12 MVP** (v0.6.9) — `NativeHttpSysGatewayAdapter` alongside `McpJungleGatewayAdapter` and `FakeMcpGatewayAdapter`. HTTP.sys lifecycle, MCP `initialize` and `tools/list` handshakes.
- **PHASE-12 follow-up** (v0.6.10) — `IWorkerSupervisor::sendStdioJsonRpc` synchronous JSON-RPC over child stdin/stdout with deadline-based polling and per-instance mutex; `tools/list` aggregates by walking each pool's first Ready instance via the bridge; `tools/call` resolves `params.name` against the cached catalog (qualified `{poolId}__{toolName}` or unique unprefixed match), acquires a lease, forwards via the bridge, re-stamps response id; bootstrapper installs URL ACL `http://+:<port>/ user=Everyone` so console-mode operators bind without elevation.
- **PHASE-13** — Win2D / Direct2D high-frequency visual surfaces in the WinUI shell (per-instance telemetry charts at 60Hz, procedural Tron HLSL backdrop, SwapChainPanel activity stream, animated saturation rings) is explicitly visual-polish work, not architectural, and is scheduled to land incrementally across v0.7.x point releases per the [plan file](handoff/realignment/PHASE-13-direct2d-shell-rendering.md).

## What landed across v0.6.x

Each v0.6.x point release is hand-authored — the [`VERSION.json`](VERSION.json) `history[]` carries the full entries.

| Version | Theme |
|---|---|
| `v0.6.10` | PHASE-12 follow-up complete: stdio bridge end-to-end, native gateway forwards `tools/call` to supervised children, bootstrapper URL ACL. |
| `v0.6.9` | PHASE-12 MVP: `NativeHttpSysGatewayAdapter` ships alongside the MCPJungle adapter; HTTP.sys lifecycle + MCP `initialize` + `tools/list`. |
| `v0.6.8` | Pool persistence (operator no longer loses pools on every restart), per-instance browser sparkline charts, telemetry events ring producer, PHASE-12 + PHASE-13 plan files. |
| `v0.6.7` | Honest-503 listener on the gateway port so LAN clients see structured JSON instead of TCP RST in supervised-mock mode. |
| `v0.6.5..v0.6.6` | Per-instance CPU/RAM telemetry sampling backend (`GetProcessTimes` + `GetProcessMemoryInfo` with first-sample baseline), MSI uninstall stale-shortcut fix, settings Apply gate fix, `preferredBindAddress` propagation. |
| `v0.6.0..v0.6.4` | The realignment program in twelve named phases (PHASE-00..PHASE-11), Claude Code Control toggle on Overview, operator-set advertised IP. |

## Realignment phase ledger

| Phase | Theme | First release |
|---|---|---|
| PHASE-00 | Repo baseline + ADR-002 | v0.6.0 |
| PHASE-01 | Provider-era residual cleanup | v0.6.0 |
| PHASE-02 | `IMcpGateway` + `McpJungleGatewayAdapter` + supervised-mock fallback | v0.6.0 |
| PHASE-03 | DNS-SD + UDP beacon + discovery document | v0.6.0 |
| PHASE-04 | Onboarding profiles per client type | v0.6.0 |
| PHASE-05 | CLU/Forsetti governance bundles per platform | v0.6.0 |
| PHASE-06 | Managed worker pools + Job Object containment | v0.6.0 |
| PHASE-07 | Lease router + autoscaling | v0.6.0 |
| PHASE-08 | Telemetry aggregator with `-1.0` honesty rule | v0.6.0 |
| PHASE-09 | Tron dashboard realignment (11 destinations) | v0.6.0 |
| PHASE-10 | Windows hardening + CI + MSI + release gate | v0.6.0 |
| PHASE-11 | Native gateway evaluation → ADR-003 | v0.6.0 |
| PHASE-12 (MVP) | `NativeHttpSysGatewayAdapter` + HTTP.sys lifecycle | v0.6.9 |
| PHASE-12 (follow-up) | Stdio bridge, real `tools/list` aggregation, real `tools/call` forwarding, URL ACL | v0.6.10 |
| PHASE-13 | Win2D / Direct2D shell rendering | scheduled v0.7.x (visual polish, not architecture) |

Each architectural phase has a written completion report in [`handoff/realignment/`](handoff/realignment/).

---

## Quick start (15 minutes)

Detailed walkthrough at [docs/wiki/Quick-Start.md](docs/wiki/Quick-Start.md). Short version:

```powershell
# 1. Build the MSI from source (or download a release artifact)
$env:VCPKG_ROOT = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg'
cmake --preset release
cmake --build build/release --config Release
ctest --test-dir build/release -C Release --output-on-failure --timeout 300
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Package-MasterControlOrchestrationServer.ps1 -Preset release -SkipBuild

# 2. Install (interactive UI)
msiexec /i "dist\packages\release\MasterControlOrchestrationServer-v0.7.1-win-x64\MasterControlOrchestrationServer-v0.7.1-win-x64.msi"

# 3. Verify (after install)
& "C:\Program Files\Master Control Orchestration Server\MasterControlBootstrapper.exe" preflight --json-output
Invoke-RestMethod http://localhost:7300/api/health    | ConvertTo-Json
Invoke-RestMethod http://localhost:7300/api/discovery | ConvertTo-Json -Depth 6

# 4. Open the firewall for LAN clients (one-shot, self-elevating)
#    See docs/wiki/Windows-Firewall-LAN-Mode.md for the full snippet that
#    creates four Profile=Private,Domain rules in one UAC prompt.

# 5. From another LAN host: confirm Bonjour discovery
Resolve-DnsName -Name _mcos._tcp.local -Type PTR -LlmnrFallback

# 6. (Optional) Switch the gateway substrate to the native HTTP.sys adapter.
#    Default is "mcpjungle" for backward compatibility with v0.6.x deployments.
#    The native adapter requires no external binary and the bootstrapper has
#    already registered the matching URL ACL during install.
$cfg = Invoke-RestMethod http://localhost:7300/api/config
$cfg.mcpGateway.type = 'native'
$cfg.mcpGateway.enabled = $true
Invoke-RestMethod http://localhost:7300/api/config -Method Post `
  -Body ($cfg | ConvertTo-Json -Depth 12) -ContentType 'application/json' `
  -Headers @{ 'X-Confirm-Unsafe' = '1' }
Restart-Service MasterControlProgram
Invoke-RestMethod http://localhost:7300/api/gateway/start -Method Post
```

The MSI installs the Windows service (`MasterControlProgram`), bundles the operator-side `mcos-control` Claude Code plugin under `share\claude-plugins\mcos-control`, registers the native gateway URL ACL via `netsh http add urlacl`, and creates Start Menu + Desktop shortcuts (both pre-checked, operator can opt out). LAN-side firewall rules are NOT created automatically — operators apply them after install. See [docs/wiki/Windows-Firewall-LAN-Mode.md](docs/wiki/Windows-Firewall-LAN-Mode.md) for the four `Profile=Private,Domain` rules (operator surface TCP 7300, MCP gateway TCP 8080, DNS-SD UDP 5353, discovery beacon UDP 7301) and a one-shot self-elevating PowerShell block that applies all four.

### Connect Claude Code to MCOS (one click)

After install, open `http://localhost:7300/` and click the **Claude Code Control** toggle on the **Overview** card. The runtime resolves the active interactive Windows user and drops `%USERPROFILE%\.claude\plugins\mcos-control` as a directory junction onto the install directory's bundled plugin source — no admin prompt, no execution-policy gymnastics. Restart Claude Code and `/mcos:status` works.

The same toggle is on the WinUI desktop shell's **Overview** page. Either surface drives the same `/api/claude-plugin/{status,toggle}` routes.

### Spawn the first MCP server / sub-agent pool

`buildDefaultConfiguration()` ships with **no pools** — the operator chooses what to supervise. Bringing up a pool is two POSTs (upsert + scale). For copy-paste recipes that exercise both `kind=mcp-server` and `kind=sub-agent` against the official `@modelcontextprotocol/server-*` reference servers, see [docs/wiki/Worker-Pools.md §10 Verified working examples](docs/wiki/Worker-Pools.md).

---

## Architecture at a glance

| Surface | What it does | Where |
|---|---|---|
| **AI-client gateway** | One advertised MCP URL; auth=none, trust=lan | `IMcpGateway` + `McpJungleGatewayAdapter` (supervised binary) **or** `NativeHttpSysGatewayAdapter` (in-process HTTP.sys) |
| **Stdio bridge** | Forwards `tools/call` JSON-RPC from gateway to supervised pool children via stdin/stdout | `IWorkerSupervisor::sendStdioJsonRpc` (PHASE-12 follow-up, v0.6.10) |
| **LAN discovery** | DNS-SD + UDP beacon + `/.well-known/mcos.json` | `DiscoveryService` + `BeaconService` |
| **Onboarding profiles** | Per-client-type config + manual instructions | `OnboardingProfileService` + `/api/onboarding/{type}` |
| **Governance bundles** | Forsetti + agentic coding instructions per platform | `GovernanceBundleService` + `/api/governance/bundles/{platform}` |
| **Worker supervision** | 7-state lifecycle, Job Object containment, redirected stdin/stdout | `WorkerSupervisor` |
| **Lease routing + autoscaling** | Sticky-session + same-type scale-out | `LeaseRouter` |
| **Telemetry aggregator** | Events ring (1024 cap), client roster, gateway snapshot, per-instance CPU/RAM sampling | `TelemetryAggregator` + `WorkerSupervisor::sampleProcessLoadLocked` |
| **Operator surface (ADR-001)** | Browser dashboard + WinUI shell | `resources/web/` + `src/MasterControlShell/` |

Full layered diagram: [docs/wiki/Architecture.md](docs/wiki/Architecture.md).

---

## Repository layout

```
master-control-dashboard-main/
├── include/MasterControl/             # Public C++ contracts, models, defaults
├── src/
│   ├── MasterControlApp/              # Runtime core: gateway adapters, lease router,
│   │                                  # supervisor, telemetry, discovery, onboarding,
│   │                                  # governance, dashboard models
│   ├── MasterControlBootstrapper/     # Installer / preflight / repair lifecycle
│   ├── MasterControlServiceHost/      # Windows service entry point + --console mode
│   ├── MasterControlShell/            # WinUI 3 desktop shell + Settings panel
│   └── MasterControlModules/          # Forsetti module registrations
├── resources/
│   ├── web/                           # Browser dashboard (HTML + JS + CSS)
│   ├── clu/                           # CLU governance profile JSON
│   └── icons/                         # App icons + MSI bitmaps
├── installer/                         # WiX v5 source for the MSI
├── scripts/                           # Build, package, sync, compliance, deployment
├── tests/                             # C++ test suite
├── docs/
│   ├── wiki/                          # Operator docs (mirror of GitHub wiki)
│   └── implementation/                # Architecture, schemas, drift inventory,
│                                      # FORBIDDEN-CONTRACT grep list
├── handoff/realignment/               # Phase manifests + completion reports
├── Forsetti-Framework-Windows-main/   # Vendored Forsetti — sealed by ADR-002 §11
└── .github/workflows/                 # CI (windows-build-test-package, release,
                                       # forsetti-compliance, ai-contributor-guard)
```

---

## Build, validate, package

| Step | Command |
|---|---|
| Configure debug | `cmake --preset debug` |
| Build debug | `cmake --build --preset debug` |
| Run tests | `ctest --preset debug --output-on-failure` |
| Forsetti compliance | `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-mastercontrol-forsetti.ps1` |
| Configure release | `cmake --preset release` |
| Build release | `cmake --build build/release --config Release` |
| Test release | `ctest --test-dir build/release -C Release --output-on-failure --timeout 300` |
| Package MSI | `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Package-MasterControlOrchestrationServer.ps1 -Preset release -SkipBuild` |

CI runs the same pipeline. See [docs/wiki/Release-Gate.md](docs/wiki/Release-Gate.md) for the release flow + the no-`workflow_dispatch` rule.

---

## Contributing

This is a proprietary repository. Operator-facing rules:

1. **No AI contributor attribution.** The `AI Contributor Guard` workflow rejects commits whose author, committer, or trailer matches an AI vendor name (`chatgpt`, `codex`, `claude`, `copilot`, `gemini`, `grok`, `openai`, `anthropic`, `deepseek`, `perplexity`, `x.ai`). Runtime references to AI products as **client types** (e.g., `clientType: "claude-code"`) are legitimate and not affected.
2. **Hand-authored documentation.** The wiki source lives in [`docs/wiki/`](docs/wiki/) — edit the markdown directly. The `docs/wiki/` tree is mirrored to the GitHub wiki.
3. **Forsetti compliance.** Every change runs through `scripts/check-mastercontrol-forsetti.ps1` in CI.
4. **FORBIDDEN-CONTRACT enforcement.** [`docs/implementation/FORBIDDEN-CONTRACT-GREP-LIST.md`](docs/implementation/FORBIDDEN-CONTRACT-GREP-LIST.md) is the machine-runnable contract — every `git grep` block must return zero matches outside documented exemptions. Eight contract groups covering provider-era removal, gateway integrity, trust model, telemetry honesty, vendoring, CI, phase scope, and dashboard honesty.
5. **Windows product gate.** Releases require a successful `Windows Build, Test, and Package` run on the target commit. The release workflow gates publication on the same-SHA gate's success and refuses to bypass.
6. **Hand-authored CHANGELOG entries.** No automated bumps. See `VERSION.json` and the operator runbook in [docs/wiki/Versions.md](docs/wiki/Versions.md).

---

## License

Proprietary. © 2026 James Daley. All Rights Reserved.
