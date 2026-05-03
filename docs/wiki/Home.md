# Master Control Orchestration Server — Operator Wiki

![version](https://img.shields.io/badge/version-v0.6.0-00f6ff?style=flat-square)
![released](https://img.shields.io/badge/released-2026--05--01-031018?style=flat-square)
![purpose](https://img.shields.io/badge/purpose-internal%20tool-1cf2c1?style=flat-square)

Internal-tool documentation. Use this wiki to install MCOS, configure it, run it day to day, and use each feature. Architecture and decisions live at the back as reference for when something is not behaving the way it should.

---

## I want to...

### Set it up
| Task | Page |
|---|---|
| Install MCOS for the first time | [Quick Start](Quick-Start) |
| Configure ports, instance name, resource % | [Configuration](Configuration) |
| Make MCOS discoverable on the LAN | [LAN Discovery](LAN-Discovery) + [Windows Firewall and LAN Mode](Operations/Windows-Firewall-LAN-Mode) |
| Install MCPJungle so the gateway is real | [Gateway](Gateway) §How to install MCPJungle |
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

### Maintain it
| Task | Page |
|---|---|
| Update to a new MCOS version | [Maintenance](Maintenance) §Upgrade |
| Repair a broken installation | [Maintenance](Maintenance) §Repair |
| Back up configuration + data | [Maintenance](Maintenance) §Backup |
| Uninstall cleanly | [Maintenance](Maintenance) §Uninstall |
| Build a fresh MSI from source | [Operations](Operations) §Build pipeline |
| Run the release gate locally | [Release Gate](Operations/Release-Gate) |

### Diagnose a problem
| Symptom | Page |
|---|---|
| LAN clients can't find the host | [Troubleshooting](Troubleshooting) §LAN discovery |
| Gateway shows `state=running, health=unknown` | [Troubleshooting](Troubleshooting) §Gateway supervised-mock |
| `Get-NetFirewallRule -DisplayName 'MCOS *'` returns nothing | [Windows Firewall and LAN Mode](Operations/Windows-Firewall-LAN-Mode) |
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
| ADR-001 LAN client identity model | [ADR-001](Architecture-Decisions/ADR-001-lan-client-control-plane) |
| ADR-002 gateway-first realignment | [ADR-002](Architecture-Decisions/ADR-002-gateway-first-mcp-realignment) |
| ADR-003 keep MCPJungle for v0.6.x | [ADR-003](Architecture-Decisions/ADR-003-mcp-gateway-substrate-decision) |
| What the IMcpGateway adapter actually does | [Gateway](Gateway) |
| How worker pools are supervised | [Worker Pools](Worker-Pools) |
| How LAN discovery is wired | [LAN Discovery](LAN-Discovery) |
| Release / packaging flow | [Operations](Operations) + [Release Gate](Operations/Release-Gate) |
| Per-release notes and PHASE-00..PHASE-11 timeline | [Versions](Versions) |
| Tron palette + motion (UI) | [Tron UI Theme](Tron-UI-Theme) |

---

## Current release

| Field | Value |
|---|---|
| Version | `v0.6.0` |
| Released | `2026-05-01` |
| Theme | Gateway-first MCP realignment (PHASE-00..PHASE-11) |
| Tag | [`v0.6.0`](https://github.com/flynn33/Master-Control-Orchestration-Server/releases/tag/v0.6.0) |
| Repository | [Master-Control-Orchestration-Server](https://github.com/flynn33/Master-Control-Orchestration-Server) |
