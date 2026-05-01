# Master Control Orchestration Server

![version](https://img.shields.io/badge/version-v0.6.0-00f6ff?style=flat-square)
![released](https://img.shields.io/badge/released-2026--05--01-031018?style=flat-square)
![platform](https://img.shields.io/badge/platform-Windows%2011%20%E2%80%A2%20Server%202022-0a1018?style=flat-square)
![toolchain](https://img.shields.io/badge/toolchain-C%2B%2B20%20%E2%80%A2%20WinUI%203%20%E2%80%A2%20CMake-00aacc?style=flat-square)
![modules](https://img.shields.io/badge/Forsetti%20modules-16-1cf2c1?style=flat-square)
![license](https://img.shields.io/badge/license-Proprietary-5a00e8?style=flat-square)

> A Windows-native **LAN client control plane** for shared MCP servers, sub-agents, and CLU-governed AI orchestration. External AI coding clients connect over the LAN under per-client privileges, share one catalog, and operate inside a Forsetti-aligned governance envelope.

```
                     ┌──────────────────────────────────────────────────┐
                     │              Master Control Orchestration       │
                     │              Server (MCOS) — host:7300          │
                     └──────────┬─────────────────────┬─────────────────┘
                                │                     │
                  X-MCOS-Client-Id                Privilege gates
                                │                     │   + CLU enforcement
   ┌────────────────┐  ┌────────┴───────┐  ┌──────────┴──────────┐
   │  AI agent A    │──┤  Identify on   ├──┤  Use shared MCP +   │
   │  (Claude Code) │  │  every request │  │  sub-agent fabric   │
   └────────────────┘  └────────────────┘  └─────────────────────┘
   ┌────────────────┐                       ┌─────────────────────┐
   │  AI agent B    │ ────────────────────▶ │  Mutate gated by    │
   │  (Codex)       │                       │  per-client privs   │
   └────────────────┘                       └─────────────────────┘
                                            ┌─────────────────────┐
                                            │ CLU defers high-    │
                                            │ impact actions to   │
                                            │ operator approval   │
                                            └─────────────────────┘
```

- **Repository:** [`master-control-dashboard`](https://github.com/flynn33/Master-Control-Orchestration-Server)
- **Architecture decision:** [ADR-001 — LAN Client Control Plane](docs/wiki/Architecture-Decisions/ADR-001-lan-client-control-plane.md)
- **Wiki:** [`docs/wiki/`](docs/wiki/) — **the canonical reference**, hand-authored with mermaid diagrams, worked examples, and decision matrices on every page
- **End-to-end proof recipe:** [`plans/PROOF-OF-WORKING/11-lan-client-end-to-end.md`](plans/PROOF-OF-WORKING/11-lan-client-end-to-end.md)
- **Changelog:** [`CHANGELOG.md`](CHANGELOG.md) — hand-authored entries, no automated bumps

---

## Why MCOS exists

Multiple AI coding agents on the same trusted LAN need to share an MCP server and sub-agent fabric without each agent operating in isolation. MCOS is the orchestration plane:

1. **Operator registers each AI agent** as a `LanClient` with a slug-form `clientId`.
2. **Operator grants privileges** — nine boolean flags covering create/modify/remove of MCP servers and sub-agents, plus client/module/governance management.
3. **Operator downloads a server-authored config bundle** and ships it to the agent's host.
4. **Agent identifies itself** on every request with the `X-MCOS-Client-Id` header.
5. **MCOS enforces privileges and CLU governance** on every privileged mutation, attributes activity to the actor, and queues high-impact decisions for operator approval.

Use is universal — every authenticated client may invoke every MCP server and sub-agent in the catalog. Only mutations are gated.

---

## Architecture at a glance

| Surface | What it does |
| --- | --- |
| **`MasterControlServiceHost.exe`** | Windows service entry point; hosts the runtime, the admin HTTP API on `:7300`, and the LAN beacon. |
| **Browser admin UI** (`resources/web`) | Operator's primary surface. Six destinations: Overview, LAN Clients, Governance, Shared Fabric, Activity, Exports. |
| **WinUI 3 desktop shell** (`src/MasterControlShell`) | Optional desktop operator surface. Currently in deferred-cleanup state from the architecture rebuild. |
| **`MasterControlBootstrapper.exe`** | Lifecycle engine for preflight / install / validate / upgrade / repair. |

The shared in-process **MCOS runtime** registers 16 Forsetti modules and exposes a single admin API consumed by the browser, the shell, and external AI clients.

---

## Quick start

```powershell
# Configure and build (Debug)
cmake --preset debug
cmake --build build\debug --config Debug

# Run the local test suite
ctest --test-dir build\debug -C Debug --output-on-failure

# Forsetti compliance + repo native checks
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-mastercontrol-forsetti.ps1

# Stage the install payload
cmake --install build\debug --config Debug --prefix dist\debug

# Build a signed release package
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Package-MasterControlOrchestrationServer.ps1 -Preset release
```

Then visit [`http://127.0.0.1:7300/`](http://127.0.0.1:7300/) in a browser.

---

## Five-minute walkthrough

```bash
# 1. Operator registers an AI agent
curl -X POST http://127.0.0.1:7300/api/clients \
  -H "Content-Type: application/json" \
  -d '{"clientId":"alpha","displayName":"Alpha","clientType":"claude_code"}'

# 2. Operator grants privileges
curl -X POST http://127.0.0.1:7300/api/clients/alpha/privileges \
  -H "Content-Type: application/json" \
  -d '{"canCreateMcpServers":true,"canCreateSubAgents":true}'

# 3. Operator downloads the config bundle
curl http://127.0.0.1:7300/api/clients/alpha/config > lan-client-alpha.json

# 4. Drop the bundle on the AI agent's host. The agent uses it to identify
#    itself on every outbound request:
curl -H "X-MCOS-Client-Id: alpha" \
     http://127.0.0.1:7300/api/client/mcp-servers
```

The full 13-step verification scenario lives in [`plans/PROOF-OF-WORKING/11-lan-client-end-to-end.md`](plans/PROOF-OF-WORKING/11-lan-client-end-to-end.md).

---

## Documentation

The wiki is **hand-authored**, comprehensive, and the canonical reference. Every page carries:

- A mental-model **mermaid diagram** at the top (flowchart, sequence diagram, state diagram, or class diagram)
- A **field-by-field reference** for every data structure or route surface introduced
- **Worked examples** in curl, PowerShell, Python, TypeScript, and/or Node where appropriate
- A **decision matrix or comparison table** that summarizes the rules at a glance
- A **common operator FAQ** at the bottom

Pages are organized by topic:

### LAN Client Control Plane

| Page | Topic |
| --- | --- |
| [LAN Clients](docs/wiki/LAN-Clients.md) | The data model, lifecycle endpoints, identification, heartbeat, activity events |
| [Privileges](docs/wiki/Privileges.md) | Nine boolean flags, autonomous-mode bypass, capability bundles |
| [Client Config Bundle](docs/wiki/Client-Config-Bundle.md) | The schemaVersion-1.0 bundle reference |
| [Governance](docs/wiki/Governance.md) | CLU enforcement, the 15 action kinds, operator approval queue |
| [Remote Client](docs/wiki/Remote-Client.md) | Onboarding an AI agent from another machine |

### Architecture & internals

| Page | Topic |
| --- | --- |
| [ADR-001](docs/wiki/Architecture-Decisions/ADR-001-lan-client-control-plane.md) | The architectural decision |
| [Architecture](docs/wiki/Architecture.md) | Runtime composition, Forsetti modules, request lifecycle |
| [API Reference](docs/wiki/API-Reference.md) | Every HTTP route exposed by the runtime |
| [Sub-Agents](docs/wiki/Sub-Agents.md) | The 7-agent specialist roster |
| [Telemetry & Activity](docs/wiki/Telemetry-and-Activity.md) | Live telemetry + activity ring |

### Operations & deployment

| Page | Topic |
| --- | --- |
| [Operations](docs/wiki/Operations.md) | Build, package, install, upgrade, uninstall |
| [Infrastructure](docs/wiki/Infrastructure.md) | Deployment shape and target hosts |
| [Troubleshooting](docs/wiki/Troubleshooting.md) | Common failures and diagnosis |

### Project & release

| Page | Topic |
| --- | --- |
| [Versions](docs/wiki/Versions.md) | Release history |
| [Automation](docs/wiki/Automation.md) | GitHub workflows that protect the repository |

---

## Contributing

This is a proprietary repository. Contributions follow these rules:

1. **No AI contributor attribution.** The repository's `AI Contributor Guard` workflow rejects commits whose author, committer, or trailer matches an AI vendor name (`chatgpt`, `codex`, `claude`, `copilot`, `gemini`, `grok`, `openai`, `anthropic`, `deepseek`, `perplexity`, `x.ai`). Runtime references to AI products (e.g., `clientType: "claude_code"`) are legitimate and not affected.
2. **Hand-authored documentation.** The previous DocSync / ReleaseAgent / WikiSync agents that auto-pushed wiki and README content as `github-actions[bot]` are **retired**. Every documentation edit is now an explicit operator action. The wiki source lives in [`docs/wiki/`](docs/wiki/) — edit the markdown directly and open a PR.
3. **Forsetti compliance.** Every change runs through `scripts/check-mastercontrol-forsetti.ps1` in CI.
4. **Windows product gate.** Releases require a successful `Windows Build, Test, and Package` run on the target commit.
5. **Hand-authored CHANGELOG entries.** No automated bumps. Categorize the change (patch / minor / major) and write the entry with the same commit. See the operator runbook in [`docs/wiki/Versions.md`](docs/wiki/Versions.md).

---

## Repository layout

```
master-control-dashboard/
├── include/MasterControl/         # Public contracts, models, defaults
├── src/
│   ├── MasterControlApp/          # Shared in-process runtime
│   ├── MasterControlServiceHost/  # Windows service entry point
│   ├── MasterControlShell/        # WinUI 3 operator shell
│   ├── MasterControlModules/      # Forsetti modules + JSON manifests
│   └── MasterControlBootstrapper/ # Installer / repair lifecycle
├── resources/
│   ├── web/                       # Browser dashboard
│   └── clu/                       # CLU governance profile
├── scripts/                       # Build, package, CI helpers
├── plans/                         # Architecture plans + proof-of-working
├── tests/                         # Native test suite
└── docs/wiki/                     # Hand-authored wiki
```

---

## License

Proprietary. © 2026 James Daley. All Rights Reserved.
