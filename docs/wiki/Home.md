# Master Control Orchestration Server

![version](https://img.shields.io/badge/version-v0.5.0-00f6ff?style=flat-square)
![released](https://img.shields.io/badge/released-2026--04--25-031018?style=flat-square)
![architecture](https://img.shields.io/badge/architecture-LAN%20Client%20Control%20Plane-1cf2c1?style=flat-square)
![modules](https://img.shields.io/badge/Forsetti%20modules-16-00aacc?style=flat-square)
![governance](https://img.shields.io/badge/governance-CLU%20%2B%20Forsetti-5a00e8?style=flat-square)
![platform](https://img.shields.io/badge/platform-Win11%20%E2%80%A2%20Server%202022-0a1018?style=flat-square)

> **A Windows-native LAN client control plane** for shared MCP servers, sub-agents, and CLU-governed AI orchestration. External AI coding agents on the LAN connect as governed users, share one MCP and sub-agent fabric, and operate inside a Forsetti-aligned governance envelope.

---

## The product in one diagram

```mermaid
flowchart LR
    classDef accent fill:#031018,stroke:#00F6FF,color:#E6FCFF,stroke-width:2px;
    classDef faint fill:#0a1018,stroke:#5A00E8,color:#8CB7C4;
    classDef client fill:#031827,stroke:#5AE8FF,color:#A8DCFF;
    classDef good fill:#031a14,stroke:#1cf2c1,color:#a8efe0;

    Operator((👤 Operator))
    Browser[Browser Admin UI<br/>:7300]:::accent

    subgraph Hosts[LAN Hosts]
        AgentA[/AI agent A<br/>Claude Code/]:::client
        AgentB[/AI agent B<br/>Codex/]:::client
        AgentC[/AI agent C<br/>any vendor/]:::client
    end

    subgraph MCOS[Master Control Orchestration Server]
        Service[[Service Host<br/>:7300]]:::accent
        Identity[[LAN Client Roster<br/>+ Privileges]]:::accent
        Catalog[(MCP Servers<br/>+ Sub-Agents)]:::good
        CLU{{CLU Governance<br/>+ Approval Queue}}:::accent
    end

    Operator --> Browser
    Browser -- "operator-fallback" --> Service
    AgentA -- "X-MCOS-Client-Id: alpha" --> Service
    AgentB -- "X-MCOS-Client-Id: bravo" --> Service
    AgentC -- "X-MCOS-Client-Id: charlie" --> Service

    Service --> Identity
    Identity -. enforces .-> Catalog
    Identity -. consults .-> CLU
    CLU -. governs .-> Catalog
    CLU -. defers high-impact .-> Operator
```

The architecture target is set by [ADR-001](Architecture-Decisions/ADR-001-lan-client-control-plane). The full nine-phase rebuild that landed it is documented in [`plans/remediation/01-gut-and-rebuild.md`](../../plans/remediation/01-gut-and-rebuild.md).

---

## Three core invariants

These three rules are non-negotiable and shape every other design decision:

> **1. Use is universal.** Every authenticated LAN client may invoke every MCP server and every sub-agent in the catalog. No per-resource visibility, no creator-only restrictions.

> **2. Mutation is gated.** Creation, modification, and removal of catalog entries pass through (a) a per-client privilege flag and (b) CLU enforcement. Both must allow.

> **3. Identity is by header on a trusted LAN.** No bearer tokens, no TLS handshake, no DPAPI secrets. The `X-MCOS-Client-Id` header carries the resolving identity. Disabled clients are refused at the door.

---

## Current release

| Field | Value |
| --- | --- |
| **Version** | `v0.5.0` |
| **Released** | `2026-04-25` |
| **Summary** | LAN Client Control Plane (ADR-001 phases 1–9 functionally complete) |
| **Forsetti modules** | 16 |
| **Repository** | [`master-control-dashboard`](https://github.com/flynn33/Master-Control-Orchestration-Server) |

See the [release history](Versions) for the full version log.

---

## Site map

### LAN Client Control Plane

The user-facing model. Start here.

| Page | What you'll learn |
| --- | --- |
| [LAN Clients](LAN-Clients) | The data model, lifecycle endpoints, identification rules, heartbeat, activity attribution |
| [Privileges](Privileges) | The nine boolean flags, the autonomous-mode bypass, capability bundles |
| [Client Config Bundle](Client-Config-Bundle) | The schemaVersion-1.0 server-authored bundle, every field referenced |
| [Governance](Governance) | CLU's two-stage gate, the 15 action kinds, the approval queue |
| [Remote Client](Remote-Client) | End-to-end onboarding flow for an AI agent on another machine |

### Architecture & internals

For implementers, integrators, and reviewers.

| Page | What you'll learn |
| --- | --- |
| [ADR-001](Architecture-Decisions/ADR-001-lan-client-control-plane) | Why the architecture is the shape it is |
| [Architecture](Architecture) | The runtime topology, request lifecycle, Forsetti modules, service container |
| [API Reference](API-Reference) | Every HTTP route — verb, path, privilege, CLU action, request body, response shape |
| [Sub-Agents](Sub-Agents) | The seven-agent specialist roster (SENTINEL through WATCHTOWER) |
| [Telemetry & Activity](Telemetry-and-Activity) | The 512-event ring buffer + telemetry stream |

### Operations & deployment

For the human running the service.

| Page | What you'll learn |
| --- | --- |
| [Operations](Operations) | Build, package, install, upgrade, repair, uninstall |
| [Infrastructure](Infrastructure) | Deployment shape, target hosts, ports, persistence paths |
| [Troubleshooting](Troubleshooting) | Common failure modes and how to diagnose them |

### Project & release

| Page | What you'll learn |
| --- | --- |
| [Versions](Versions) | Release history with the rationale per release |
| [Automation](Automation) | The three GitHub Actions workflows that protect the repo |

---

## Five-minute walkthrough

Operator on `MCOS-HOST`, AI agent on a remote workstation:

```mermaid
sequenceDiagram
    autonumber
    participant Op as Operator
    participant MCOS
    participant Agent as AI Agent on host A

    Op->>MCOS: POST /api/clients<br/>{clientId:"alpha", clientType:"claude_code"}
    MCOS-->>Op: 200 LAN client registered

    Op->>MCOS: POST /api/clients/alpha/privileges<br/>{canCreateMcpServers:true}
    MCOS-->>Op: 200 LAN client privileges updated

    Op->>MCOS: GET /api/clients/alpha/config
    MCOS-->>Op: 200 {schemaVersion:"1.0", mcosServer, identification, ...}
    Op-)Agent: Hand the bundle to host A

    Agent->>MCOS: POST /api/runtime/mcp-servers<br/>X-MCOS-Client-Id: alpha
    Note over MCOS: 1. Resolve identity → alpha<br/>2. canCreateMcpServers? ✓<br/>3. CLU.enforceAction → Allow
    MCOS-->>Agent: 200 succeeded

    Agent->>MCOS: GET /api/client/mcp-servers<br/>X-MCOS-Client-Id: alpha
    MCOS-->>Agent: 200 [shared catalog]
```

Walk it yourself with the curl scripts in [`plans/PROOF-OF-WORKING/11-lan-client-end-to-end.md`](../../plans/PROOF-OF-WORKING/11-lan-client-end-to-end.md).

---

## Three-line product pitch

1. **Register every AI agent on the LAN.** Each agent gets a server-side `LanClient` record carrying nine privilege toggles plus an autonomous-mode flag. Identity is by header on a trusted LAN.
2. **Share one MCP and sub-agent fabric.** Every authenticated client may use every catalog entry. Creation, modification, and removal are gated by per-client privileges. Autonomous clients may build out the shared fabric without per-action approval.
3. **Govern from one console.** Every privileged mutation passes through CLU, attributed to the resolving actor, captured in the activity ring, and visible in the browser dashboard. Deferred decisions queue for operator approval.

---

## What's not here yet

The nine-phase rebuild leaves a few items deliberately out-of-scope for `v0.5.0`:

- **WinUI 3 desktop shell** — kept in a deferred-cleanup state from Phase 2b. The browser admin UI delivers everything Phase 8 needed; the shell rebuild is queued as a separate track.
- **Captured proof receipt.** [`plans/PROOF-OF-WORKING/11-lan-client-end-to-end.md`](../../plans/PROOF-OF-WORKING/11-lan-client-end-to-end.md) is currently a verification recipe; running the build and capturing the live receipt is the operator's next step.
- **Hardening track.** ADR-001 locked decisions kept the trusted-LAN posture (no auth, no TLS). A future track can add bearer tokens or mTLS without re-architecting; the bundle's `identification` field shape leaves room.

---

> **Tip:** The wiki is hand-authored. If a section is wrong, file an issue or a pull request — the repository's AI Contributor Guard will reject AI-attributed commits, but operator edits are welcome.
