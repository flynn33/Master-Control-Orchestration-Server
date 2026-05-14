# Master Control Orchestration Server — Operator Wiki

![version](https://img.shields.io/badge/version-v0.10.11-00f6ff?style=flat-square)
![released](https://img.shields.io/badge/released-2026--05--11-031018?style=flat-square)
![architecture](https://img.shields.io/badge/architecture-complete-1cf2c1?style=flat-square)
![purpose](https://img.shields.io/badge/purpose-internal%20tool-5a00e8?style=flat-square)

Internal-tool documentation. Use this wiki to install MCOS, configure it, run it day to day, and use each feature. Architecture and decisions live at the back as reference for when something is not behaving the way it should.

> **v0.10.11 sits on top of the v0.7.0 production architecture.** the legacy external gateway was retired in v0.9.0 — the only shipping gateway substrate is now the in-process Windows-native HTTP.sys adapter. The v0.9.x and v0.10.x lines added the Supervisor Agent Assignment Wizard (operator picks one supervisor model — `chatgpt` / `claude` / `grok` — and MCOS issues a LAN-routable config the client uses to bind), a footer-style tile-grid renderer used by Telemetry + Runtime + the cross-tab SUB-AGENT GRID, persistent Diagnostics logging, a hot-deploy helper (`scripts\Deploy-LocalLive.ps1`), and pool orchestration scaffolding under `.claude/`.

---

## I want to...

### Set it up
| Task | Page |
|---|---|
| Install MCOS for the first time | [Quick Start](Quick-Start) |
| Configure ports, instance name, resource % | [Configuration](Configuration) |
| Make MCOS discoverable on the LAN | [LAN Discovery](LAN-Discovery) + [Windows Firewall and LAN Mode](Windows-Firewall-LAN-Mode) |
| Gateway substrate (native HTTP.sys is the only shipping path as of v0.9.0) | [Gateway](Gateway) §Native HTTP.sys substrate |
| Assign a supervisor model (chatgpt / claude / grok) | [Sub-Agents](Sub-Agents) §Supervisor Agent Assignment Wizard |
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
| Version | `v0.10.11` |
| Released | `2026-05-11` |
| Theme | LAN MCP Gateway + Supervisor Agent Assignment Wizard + footer-style tile-grid shell |
| Tag | [`v0.10.11`](https://github.com/flynn33/Master-Control-Orchestration-Server/releases/tag/v0.10.11) |
| Gateway substrate | `native` (in-process Windows HTTP.sys) — only shipping substrate as of v0.9.0. native HTTP.sys gateway retired per operator directive. |
| Supervisor wizard | Operator selects one of `chatgpt` / `claude` / `grok`; MCOS issues a LAN-routable config bundle the supervisor client uses to bind. Lifecycle off → config_generated → pending_connection → connected → disconnected \| revoked. |
| Boot self-tests | 39 probes (was ~30 at v0.7.0). Failures dual-emit to the persistent Diagnostics log. |
| Scheduled next | v1.0.0+ candidates: CLU Phase 2/3 (`enforceAction` wiring), PHASE-14 DiagnosticsSectionControl, telemetry log rotation, tile-grid expand-on-click. |
| Repository | [Master-Control-Orchestration-Server](https://github.com/flynn33/Master-Control-Orchestration-Server) |
