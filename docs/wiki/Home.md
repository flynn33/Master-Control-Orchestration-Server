# Master Control Orchestration Server

![version](https://img.shields.io/badge/version-v0.6.0-00f6ff?style=flat-square)
![released](https://img.shields.io/badge/released-2026--05--01-031018?style=flat-square)
![architecture](https://img.shields.io/badge/architecture-LAN%20MCP%20Gateway%20Host-1cf2c1?style=flat-square)
![governance](https://img.shields.io/badge/governance-CLU%20%2B%20Forsetti-5a00e8?style=flat-square)
![platform](https://img.shields.io/badge/platform-Win11%20%E2%80%A2%20Server%202022-0a1018?style=flat-square)
![toolchain](https://img.shields.io/badge/toolchain-C%2B%2B20%20%E2%80%A2%20WinUI%203-00aacc?style=flat-square)

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

    subgraph LANClients[LAN AI Clients]
        direction TB
        ClaudeCode[/Claude Code/]:::client
        Codex[/Codex/]:::client
        Grok[/Grok/]:::client
        ChatGPT[/ChatGPT connector-edge/]:::client
        Generic[/Generic MCP/]:::client
    end

    subgraph MCOS[Master Control Orchestration Server]
        direction TB
        Discovery[LAN Discovery<br/>DNS-SD + UDP beacon]:::accent
        Gateway[MCP Gateway<br/>MCPJungle adapter]:::accent
        Onboarding[Onboarding Profiles<br/>per client type]:::accent
        Governance[Governance Bundles<br/>Windows / macOS / iOS]:::accent
        Supervisor[Worker Supervisor<br/>+ Lease Router]:::accent
        Telemetry[Telemetry Aggregator<br/>events / clients / gateway]:::accent
        Pools[(Managed Endpoint Pools<br/>MCP servers + sub-agents)]:::good
    end

    Operator -->|Browser dashboard| Telemetry
    LANClients -->|"DNS-SD discovery (auth=none, trust=lan)"| Discovery
    Discovery --> Gateway
    LANClients -->|MCP requests| Gateway
    Gateway -->|registers logical pools with| Supervisor
    Supervisor --> Pools
    Pools -->|heartbeat metrics| Telemetry
    LANClients -.->|fetch on first connect| Onboarding
    LANClients -.->|fetch on demand| Governance
```

The architecture target is the **gateway-first MCP host** declared in [ADR-002](Architecture-Decisions/ADR-002-gateway-first-mcp-realignment) and locked at the substrate level by [ADR-003](Architecture-Decisions/ADR-003-mcp-gateway-substrate-decision). The original LAN client identity model in [ADR-001](Architecture-Decisions/ADR-001-lan-client-control-plane) survives as the **operator surface** that coexists with the AI-client gateway surface.

---

## Current release

| Field | Value |
| --- | --- |
| **Version** | `v0.6.0` |
| **Released** | `2026-05-01` |
| **Summary** | Gateway-first MCP realignment (ADR-002 / ADR-003). PHASE-00 through PHASE-11 complete. The product is a LAN MCP gateway host; AI clients connect to one advertised endpoint and consume managed worker pools, governance bundles, and onboarding profiles. |
| **Repository** | [Master-Control-Orchestration-Server](https://github.com/flynn33/Master-Control-Orchestration-Server) |
| **License** | Proprietary |

---

## Quick paths

- **First-time install** → [Quick Start](Quick-Start)
- **Connect an AI client** → [Onboarding](Onboarding)
- **Understand how it works** → [Architecture](Architecture)
- **Why each design choice was made** → [Architecture Decisions](Architecture-Decisions)
- **Operate it day-to-day** → [Operations](Operations)
- **Diagnose a problem** → [Troubleshooting](Troubleshooting)

---

## Three-line product pitch

1. **One advertised endpoint.** AI clients on the LAN find MCOS via Bonjour-compatible DNS-SD and connect to a single MCP gateway URL. No per-backend wiring on the client side.
2. **Honest infrastructure.** Worker pools are supervised under Windows Job Objects, telemetry uses a `-1.0` "unavailable" sentinel rather than fabricating values, and the dashboard surfaces real state — not aspirational state.
3. **Reversible by construction.** Every gateway-related decision sits behind the `IMcpGateway` adapter. The MCPJungle substrate is supervised, not vendored; it can be replaced with a native HTTP.sys gateway whenever operational evidence justifies the swap, without breaking any client contract.

---

## Site map

### Architecture and design
| Page | Topic |
| --- | --- |
| [Architecture](Architecture) | Runtime composition, layers, request flows |
| [Architecture Decisions](Architecture-Decisions) | ADR-001 / 002 / 003 with summaries and supersession history |
| [Gateway](Gateway) | `IMcpGateway` adapter, MCPJungle substrate, supervised-mock fallback |
| [Worker Pools](Worker-Pools) | Managed endpoint pools, 7-state lifecycle, lease routing, autoscaling |
| [LAN Discovery](LAN-Discovery) | DNS-SD service types, UDP beacon, the discovery document |
| [Telemetry and Activity](Telemetry-and-Activity) | Events ring, client roster, gateway traffic, honest `-1.0` sentinel |
| [API Reference](API-Reference) | Every HTTP route exposed by the runtime |

### Onboarding and governance
| Page | Topic |
| --- | --- |
| [Quick Start](Quick-Start) | Install MSI → first run → verify on LAN |
| [Onboarding](Onboarding) | Per-client-type profiles for Claude Code / Codex / Grok / ChatGPT / generic-MCP |
| [CLU Governance](CLU-Governance) | Forsetti-aligned bundles, profile, decision policy, approval queue |
| [LAN Clients](LAN-Clients) | ADR-001 operator surface — per-client identity, privileges, autonomous mode |
| [Privileges](Privileges) | Nine-flag privilege model on the operator surface |
| [Client Config Bundle](Client-Config-Bundle) | Server-authored bundle for the operator surface |

### Operations
| Page | Topic |
| --- | --- |
| [Operations](Operations) | Build, validate, package, install, upgrade, repair, uninstall |
| [Windows Firewall and LAN Mode](Windows-Firewall-LAN-Mode) | Trust model, firewall rules, validation snippets |
| [Packaging and Gateway Binary](Packaging-and-Gateway-Binary) | What the MSI installs, why the gateway is operator-installed |
| [Release Gate](Release-Gate) | The CI workflow pair, no-`workflow_dispatch` rule, tag → release flow |
| [Dashboard](Dashboard) | Tour of the 11 browser dashboard destinations |
| [Sub-Agents](Sub-Agents) | Sub-agent roster within managed pools |
| [Infrastructure](Infrastructure) | Deployment shape and target hosts |

### Project
| Page | Topic |
| --- | --- |
| [Versions](Versions) | Release history including the realignment program (PHASE-00..PHASE-11) |
| [Tron UI Theme](Tron-UI-Theme) | Palette, typography, motion |
| [Automation](Automation) | GitHub agents that maintain this repo |
| [Troubleshooting](Troubleshooting) | Common failures and diagnosis |
