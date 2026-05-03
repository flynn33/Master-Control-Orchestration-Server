# Master Control Orchestration Server

![version](https://img.shields.io/badge/version-v0.6.0-00f6ff?style=flat-square)
![released](https://img.shields.io/badge/released-2026--05--01-031018?style=flat-square)
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
        Gateway[MCP Gateway<br/>MCPJungle adapter]:::accent
        Onboarding[Onboarding Profiles<br/>per client type]:::accent
        Governance[Governance Bundles<br/>Windows / macOS / iOS]:::accent
        Supervisor[Worker Supervisor<br/>+ Lease Router]:::accent
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

The architecture target is the **gateway-first MCP host** declared in [ADR-002](docs/wiki/Architecture-Decisions/ADR-002-gateway-first-mcp-realignment.md) and locked at the substrate level by [ADR-003](docs/wiki/Architecture-Decisions/ADR-003-mcp-gateway-substrate-decision.md). The original [ADR-001 LAN client identity model](docs/wiki/Architecture-Decisions/ADR-001-lan-client-control-plane.md) survives as the operator surface that coexists with the AI-client gateway surface.

---

## Quick links

- **Wiki (operator-facing)** → [github.com/flynn33/Master-Control-Orchestration-Server/wiki](https://github.com/flynn33/Master-Control-Orchestration-Server/wiki)
- **Quick Start** → [docs/wiki/Quick-Start.md](docs/wiki/Quick-Start.md)
- **Architecture** → [docs/wiki/Architecture.md](docs/wiki/Architecture.md)
- **Architecture Decisions** → [docs/wiki/Architecture-Decisions.md](docs/wiki/Architecture-Decisions.md)
- **Onboarding an AI client** → [docs/wiki/Onboarding.md](docs/wiki/Onboarding.md)
- **CHANGELOG** → [`CHANGELOG.md`](CHANGELOG.md)

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

## v0.6.0 — what shipped

The realignment program in twelve named phases (PHASE-00..PHASE-11):

| Phase | Theme | Commit |
|---|---|---|
| PHASE-00 | Repo baseline + ADR-002 | `d8758ac` |
| PHASE-01 | Provider-era residual cleanup | `a784ffb` |
| PHASE-02 | `IMcpGateway` + `McpJungleGatewayAdapter` + supervised-mock fallback | `86695c3` |
| PHASE-03 | DNS-SD + UDP beacon + discovery document | `6f37cf0` |
| PHASE-04 | Onboarding profiles per client type | `f2d51bc` |
| PHASE-05 | CLU/Forsetti governance bundles per platform | `aa4087a` |
| PHASE-06 | Managed worker pools + Job Object containment | `c8077f0` |
| PHASE-07 | Lease router + autoscaling | `0cb9b48` |
| PHASE-08 | Telemetry aggregator with `-1.0` honesty rule | `228e944` |
| PHASE-09 | Tron dashboard realignment (11 destinations) | `c241440` |
| PHASE-10 | Windows hardening + CI + MSI + release gate | `d98b074` |
| PHASE-11 | Native gateway evaluation → ADR-003 | `f21e868` |

Each phase has a written completion report in [`handoff/realignment/`](handoff/realignment/).

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
msiexec /i "dist\packages\release\MasterControlOrchestrationServer-v0.6.0-win-x64\MasterControlOrchestrationServer-v0.6.0-win-x64.msi"

# 3. Verify (after install)
& "C:\Program Files\Master Control Orchestration Server\MasterControlBootstrapper.exe" preflight --json-output
Invoke-RestMethod http://localhost:7300/api/health    | ConvertTo-Json
Invoke-RestMethod http://localhost:7300/api/discovery | ConvertTo-Json -Depth 6

# 4. From another LAN host: confirm Bonjour discovery
Resolve-DnsName -Name _mcos._tcp.local -Type PTR -LlmnrFallback
```

The MSI installs the Windows service, registers four `Profile=Private,Domain` firewall rules covering the operator surface (TCP), the MCP gateway (TCP), DNS-SD (UDP 5353), and the discovery beacon (UDP), and creates Start Menu + Desktop shortcuts (both pre-checked, operator can opt out).

---

## Architecture at a glance

| Surface | What it does | Where |
|---|---|---|
| **AI-client gateway** | One advertised MCP URL; auth=none, trust=lan | `IMcpGateway` + `McpJungleGatewayAdapter` |
| **LAN discovery** | DNS-SD + UDP beacon + `/.well-known/mcos.json` | `DiscoveryService` + `BeaconService` |
| **Onboarding profiles** | Per-client-type config + manual instructions | `OnboardingProfileService` + `/api/onboarding/{type}` |
| **Governance bundles** | Forsetti + agentic coding instructions per platform | `GovernanceBundleService` + `/api/governance/bundles/{platform}` |
| **Worker supervision** | 7-state lifecycle, Job Object containment | `WorkerSupervisor` |
| **Lease routing + autoscaling** | Sticky-session + same-type scale-out | `LeaseRouter` |
| **Telemetry aggregator** | Events ring (1024 cap), client roster, gateway snapshot | `TelemetryAggregator` |
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

CI runs the same pipeline. See [docs/wiki/Operations/Release-Gate.md](docs/wiki/Operations/Release-Gate.md) for the release flow + the no-`workflow_dispatch` rule.

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
