# mcos-control — Claude Code plugin

![purpose](https://img.shields.io/badge/purpose-internal%20tool-1cf2c1?style=flat-square)
![bridge](https://img.shields.io/badge/bridge-stdlib%20Python-00f6ff?style=flat-square)
![version](https://img.shields.io/badge/plugin-v1.0.0-00aacc?style=flat-square)

Claude Code plugin that gives Claude direct, real-time control of a running Master Control Orchestration Server. Read live state, configure settings, register pools, manage governance, import Forsetti modules — all via the MCOS admin API.

Pure-stdlib Python bridge (no pip dependencies), 43 tools, 5 sub-agents, 12 slash commands, 1 procedural skill.

---

## What it does

| | |
|---|---|
| **Read** | Health, gateway state, pool state + lease + saturation, telemetry events, presence roster, governance approvals, discovery doc, onboarding profiles, config, registered Forsetti modules |
| **Write** | Apply config changes, register/scale/drain/remove pools, acquire/release leases, start/stop gateway, approve/reject governance actions, manage operator-surface clients + privileges, import/enable/disable Forsetti modules |
| **Local diagnostics** | `Get-Service` status, tail `events.jsonl`, `Get-NetFirewallRule -DisplayName 'MCOS *'`, `Resolve-DnsName _mcos._tcp.local` |

Every destructive operation requires `confirm: true`. The bridge refuses without it.

---

## Architecture

```
Claude Code session
   |
   |  (MCP protocol, stdio JSON-RPC)
   v
mcos-bridge MCP server  (Python, in this plugin)
   |
   |  HTTP requests
   v
MCOS admin API at MCOS_BASE_URL  (default http://localhost:7300)
```

The bridge is the audit boundary. It speaks MCP to Claude Code on stdio and translates tool calls to documented HTTP routes against the MCOS admin API. It does not bypass the architecture: no direct runtime access, no Forsetti vendoring touch, no app-layer auth bolted on.

---

## Installation

### From this repo (the source of truth)

The plugin lives at `.claude-plugin/mcos-control/` in the MCOS repo. To install in your local Claude Code:

```bash
# Either: copy/symlink the plugin dir into ~/.claude/plugins/
cp -r <repo-root>/.claude-plugin/mcos-control ~/.claude/plugins/

# Or: install via the Claude Code CLI
claude plugin add <repo-root>/.claude-plugin/mcos-control
```

After install, Claude Code picks up:

- `mcp-servers/mcos-bridge/server.py` — the bridge (registered via `plugin.json`)
- `agents/mcos-*.md` — five sub-agents
- `commands/*.md` — 12 slash commands prefixed `/mcos:`
- `skills/mcos-operations/SKILL.md` — the operations runbook

### Configuration

Set the base URL of the MCOS instance the plugin should talk to:

```bash
# bash / zsh
export MCOS_BASE_URL="http://localhost:7300"
export MCOS_TIMEOUT="10.0"

# PowerShell
$env:MCOS_BASE_URL = "http://localhost:7300"
$env:MCOS_TIMEOUT  = "10.0"
```

If unset, the bridge defaults to `http://localhost:7300` and a 10-second timeout per call.

---

## Slash commands

| Command | What it does |
|---|---|
| `/mcos:status` | Quick MCOS status — service, gateway, pools, presence, governance posture, in one view |
| `/mcos:diagnose` | Full diagnostic sweep — service, gateway, pools, firewall, DNS-SD, recent error events |
| `/mcos:pool-add` | Register a managed worker pool (hands off to `mcos-pool-architect`) |
| `/mcos:pool-drain <poolId>` | Drain a pool gracefully |
| `/mcos:pool-scale <poolId>` | Force a pool to its `minInstances` |
| `/mcos:pool-remove <poolId>` | Remove a pool definition (destructive) |
| `/mcos:onboard <clientType>` | Pull and present an onboarding profile |
| `/mcos:firewall-rules` | Emit the four `New-NetFirewallRule` snippets templated with live ports |
| `/mcos:backup` | Walk the operator through a config + state backup |
| `/mcos:governance-approve [id]` | Review pending CLU approvals (hands off to `mcos-governance-reviewer`) |
| `/mcos:forsetti-import <manifest.json>` | Import a Forsetti module manifest |
| `/mcos:activity [severity] [category]` | Show recent telemetry events |

---

## Sub-agents

| Agent | When it triggers |
|---|---|
| `mcos-operator` | Live operations — health, config, pools, governance, log inspection |
| `mcos-installer` | First-run setup — instance label, ports, gateway substrate, first pool, firewall, discovery |
| `mcos-troubleshooter` | Failure diagnosis — five named diagnostic chains (A–E) |
| `mcos-pool-architect` | Pool design + sizing — heuristics for scale policy, drain, health probe |
| `mcos-governance-reviewer` | Approval queue review — surfaces items, never auto-approves |

---

## MCP tool catalog

The bridge exposes 43 tools. Full reference in `skills/mcos-operations/SKILL.md`. Categories:

- **Read** (20 tools) — `mcos_health`, `mcos_dashboard`, `mcos_gateway_*`, `mcos_pools_list`, `mcos_pool_*`, `mcos_telemetry_*`, `mcos_discovery`, `mcos_onboarding`, `mcos_governance_*`, `mcos_clients_list`, `mcos_config_get`, `mcos_activity`, `mcos_forsetti_modules`
- **Write — config + telemetry** (2 tools) — `mcos_config_update`, `mcos_telemetry_heartbeat`
- **Write — pools** (6 tools) — `mcos_pool_upsert`, `mcos_pool_scale`, `mcos_pool_drain`, `mcos_pool_remove`, `mcos_lease_acquire`, `mcos_lease_release`
- **Write — gateway** (2 tools) — `mcos_gateway_start`, `mcos_gateway_stop`
- **Write — governance** (2 tools) — `mcos_governance_approve`, `mcos_governance_reject`
- **Write — operator surface** (4 tools) — `mcos_client_register`, `mcos_client_privileges`, `mcos_client_enable`, `mcos_client_disable`
- **Write — Forsetti** (3 tools) — `mcos_forsetti_module_import`, `mcos_forsetti_module_enable`, `mcos_forsetti_module_disable`
- **Local diagnostics** (4 tools) — `mcos_service_status`, `mcos_logs_tail`, `mcos_firewall_check`, `mcos_dns_sd_check`

Six destructive tools require `confirm: true`: `mcos_pool_drain`, `mcos_pool_remove`, `mcos_gateway_stop`, `mcos_governance_reject`, `mcos_client_disable`, `mcos_forsetti_module_disable`.

---

## Trust posture

| Concern | Resolution |
|---|---|
| **Auth to MCOS** | LAN-trusted (ADR-001 §3 / ADR-002 §1) — no app-layer auth on the operator surface. The plugin reaches MCOS over the LAN; firewall scoping is the load-bearing trust control. The plugin does not embed bearer tokens or any other auth shim. |
| **Destructive ops** | Six tools require explicit `confirm: true`. Without it, they return `errorCode: CONFIRM_REQUIRED` and a description of what would have happened. |
| **Audit trail** | Every write is recorded in MCOS's telemetry events ring (`mcos_telemetry_events`) with `category: System` and a source field. Operators can review what the plugin did. |
| **Failure modes** | Network errors, 5xx, and 4xx all produce structured `{ok: false, error, errorCode, status, hint}` rather than exceptions. Bridge never crashes from a bad MCOS response. |
| **No bypass of architecture** | Bridge talks documented HTTP routes only. Does not poke runtime memory, does not modify `Forsetti-Framework-Windows-main/` (sealed by ADR-002 §11), does not register fake gateway adapters. |
| **No AI attribution** | Plugin authored as part of the MCOS realignment work. Claude Code drives it, but commits and contributions follow the repo's no-AI-contributor rule. |

---

## Smoke test

After install, Claude Code should be able to run:

```
/mcos:status
```

…against a running MCOS service, and produce:

```
MCOS STATUS · mcos-<instance>
─────────────────────────────────────────────────────────────
Service           : ok
Gateway           : running · healthy · mcpjungle
Advertised URL    : http://0.0.0.0:8080/mcp
Pools             : 1 pool(s) · 1 ready · 0 active leases
Connected clients : 0
Governance        : pass · 0 pending approvals
─────────────────────────────────────────────────────────────
```

Or if MCOS isn't running:

```
Service: unreachable (Network error: Connection refused)
ATTENTION: Cannot reach MCOS at http://localhost:7300. Check Get-Service MasterControlOrchestrationServer.
```

---

## Bridge internals

`mcp-servers/mcos-bridge/server.py` — single file, ~600 lines, Python 3.9+ stdlib only:

- `urllib.request` for HTTP
- `subprocess` for local PowerShell diagnostic tools
- JSON-RPC over stdin/stdout per the MCP 2024-11-05 protocol
- Tool registry as a flat list of `(name, schema, handler)` tuples — easy to add new tools

To add a tool: write a `t_*` function, append a tuple to `TOOLS_REGISTRY`, restart the plugin.

---

## See also

- [MCOS wiki Home](https://github.com/flynn33/Master-Control-Orchestration-Server/wiki) — operator documentation
- [Daily Operations](https://github.com/flynn33/Master-Control-Orchestration-Server/wiki/Daily-Operations) — reference runbook
- [Architecture](https://github.com/flynn33/Master-Control-Orchestration-Server/wiki/Architecture) — runtime composition
- [ADR-001](https://github.com/flynn33/Master-Control-Orchestration-Server/wiki/ADR-001-lan-client-control-plane), [ADR-002](https://github.com/flynn33/Master-Control-Orchestration-Server/wiki/ADR-002-gateway-first-mcp-realignment), [ADR-003](https://github.com/flynn33/Master-Control-Orchestration-Server/wiki/ADR-003-mcp-gateway-substrate-decision) — design decisions
