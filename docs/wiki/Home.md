# Master Control Orchestration Server — Operator Wiki

![version](https://img.shields.io/badge/version-v0.7.0-00f6ff?style=flat-square)
![released](https://img.shields.io/badge/released-2026--05--05-031018?style=flat-square)
![architecture](https://img.shields.io/badge/architecture-complete-1cf2c1?style=flat-square)
![purpose](https://img.shields.io/badge/purpose-internal%20tool-5a00e8?style=flat-square)

Internal-tool documentation. Use this wiki to install MCOS, configure it, run it day to day, and use each feature. Architecture and decisions live at the back as reference for when something is not behaving the way it should.

> **v0.7.0 ships the production architecture milestone.** Every numbered phase from PHASE-00 through PHASE-12 follow-up is delivered. Both gateway substrates ship and are operator-selectable: supervised MCPJungle (the conservative path) and Windows-native HTTP.sys (in-process, no external binary). Pick by setting `mcpGateway.type` in `mcos.json` or via `POST /api/config`. PHASE-13 (Win2D / Direct2D shell rendering) is visual-polish work scheduled for v0.7.x point releases.

---

## I want to...

### Set it up
| Task | Page |
|---|---|
| Install MCOS for the first time | [Quick Start](Quick-Start) |
| Configure ports, instance name, resource % | [Configuration](Configuration) |
| Make MCOS discoverable on the LAN | [LAN Discovery](LAN-Discovery) + [Windows Firewall and LAN Mode](Windows-Firewall-LAN-Mode) |
| Pick a gateway substrate (native HTTP.sys vs supervised MCPJungle) | [Gateway](Gateway) §Substrate selection |
| Install MCPJungle (only if `mcpGateway.type=mcpjungle`) | [Gateway](Gateway) §How to install MCPJungle |
| Add Start Menu / Desktop shortcuts (or remove them) | [Maintenance](Maintenance) §Shortcuts |

### Connect AI clients
| Task | Page |
|---|---|
| Onboard Claude Code | [Onboarding](Onboarding) §Claude Code |
| Onboard Codex | [Onboarding](Onboarding) §Codex |
| Onboard Grok | [Onboarding](Onboarding) §Grok |
| Onboard ChatGPT (connector-edge) | [Onboarding](Onboarding) §ChatGPT |
| Onboard a generic MCP client | [Onboarding](Onboarding) §Generic |
| Hand a client a governance bundle | [CLU Governance](CLU-Governance) §How to download a bundle |

### Use it day to day
| Task | Page |
|---|---|
| Check that everything is healthy | [Daily Operations](Daily-Operations) §Health check |
| See what AI clients are connected right now | [Daily Operations](Daily-Operations) §Connected clients |
| See what just happened on the host | [Daily Operations](Daily-Operations) §Activity stream |
| Add a managed MCP server pool | [Worker Pools](Worker-Pools) §How to add a pool |
| Drain a pool for maintenance | [Worker Pools](Worker-Pools) §How to drain |
| Force scale a pool to its minimum | [Worker Pools](Worker-Pools) §How to scale |
| Approve / reject a pending governance action | [Governance](Governance) §Approval queue |
| Change a setting (port, beacon, resources) | [Configuration](Configuration) §Editing live |
| Pull a config bundle for a LAN client | [Client Config Bundle](Client-Config-Bundle) |
| Drive MCOS from a Claude Code session | [Claude Code Plugin](Claude-Code-Plugin) |

### Maintain it
| Task | Page |
|---|---|
| Update to a new MCOS version | [Maintenance](Maintenance) §Upgrade |
| Repair a broken installation | [Maintenance](Maintenance) §Repair |
| Back up configuration + data | [Maintenance](Maintenance) §Backup |
| Uninstall cleanly | [Maintenance](Maintenance) §Uninstall |
| Build a fresh MSI from source | [Operations](Operations) §Build pipeline |
| Run the release gate locally | [Release Gate](Release-Gate) |

### Diagnose a problem
| Symptom | Page |
|---|---|
| LAN clients can't find the host | [Troubleshooting](Troubleshooting) §LAN discovery |
| Gateway shows `state=running, health=unknown` | [Troubleshooting](Troubleshooting) §Gateway supervised-mock |
| `Get-NetFirewallRule -DisplayName 'MCOS *'` returns nothing | [Windows Firewall and LAN Mode](Windows-Firewall-LAN-Mode) |
| Service won't start | [Troubleshooting](Troubleshooting) §Service hung |
| Clients connect but get 403 on a privileged action | [Troubleshooting](Troubleshooting) §Missing privilege |
| Where are the logs? | [Troubleshooting](Troubleshooting) §Where are the logs |

### Look something up
| Topic | Page |
|---|---|
| Every HTTP route the runtime exposes | [API Reference](API-Reference) |
| Every field of `mcos.json` | [Configuration](Configuration) |
| Dashboard destinations and what each one does | [Dashboard](Dashboard) |
| What's tracked as telemetry, what's "unavailable" vs "idle" | [Telemetry and Activity](Telemetry-and-Activity) |

---

## Reference (architecture and decisions)

These pages explain why MCOS works the way it does. Read when something is not behaving as expected, or before making structural changes.

| Reference | Page |
|---|---|
| Runtime composition + diagrams | [Architecture](Architecture) |
| Why each design choice was made | [Architecture Decisions](Architecture-Decisions) |
| ADR-001 LAN client identity model | [ADR-001](ADR-001-lan-client-control-plane) |
| ADR-002 gateway-first realignment | [ADR-002](ADR-002-gateway-first-mcp-realignment) |
| ADR-003 substrate decision (status-updated for PHASE-12 ship) | [ADR-003](ADR-003-mcp-gateway-substrate-decision) |
| What the IMcpGateway adapter actually does | [Gateway](Gateway) |
| How worker pools are supervised | [Worker Pools](Worker-Pools) |
| How LAN discovery is wired | [LAN Discovery](LAN-Discovery) |
| Release / packaging flow | [Operations](Operations) + [Release Gate](Release-Gate) |
| Per-release notes (v0.6.0..v0.7.0) and full phase ledger | [Versions](Versions) |
| Tron palette + motion (UI) | [Tron UI Theme](Tron-UI-Theme) |

---

## Current release

| Field | Value |
|---|---|
| Version | `v0.7.0` |
| Released | `2026-05-05` |
| Theme | Production milestone — architecture complete (PHASE-00..PHASE-12 follow-up) |
| Tag | [`v0.7.0`](https://github.com/flynn33/Master-Control-Orchestration-Server/releases/tag/v0.7.0) |
| Gateway substrates | `mcpjungle` (supervised binary) **and** `native` (in-process HTTP.sys) — operator-selectable |
| Stdio bridge | shipped in v0.6.10; native gateway forwards `tools/list` + `tools/call` to supervised pool children |
| Scheduled next | PHASE-13 visual polish (Win2D charts, Tron HLSL backdrop, SwapChainPanel activity stream) — v0.7.x point releases |
| Repository | [Master-Control-Orchestration-Server](https://github.com/flynn33/Master-Control-Orchestration-Server) |
