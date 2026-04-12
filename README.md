# Master Control Orchestration Server

![version](https://img.shields.io/badge/version-v0.2.11-00f6ff?style=flat-square) ![released](https://img.shields.io/badge/released-2026--04--12-031018?style=flat-square) ![platform](https://img.shields.io/badge/platform-Windows%2011%20/%20Server%202022-0a1018?style=flat-square) ![toolchain](https://img.shields.io/badge/toolchain-C++20%20·%20WinUI%203%20·%20CMake-00aacc?style=flat-square) ![license](https://img.shields.io/badge/license-Proprietary-5a00e8?style=flat-square)

> Forsetti-compliant Windows orchestration control plane for MCP services, AI provider routing, 
> CLU governance, sub-agents, platform gateways, telemetry, and browser-based operations — 
> all delivered as a single Tron-themed product.

- **Repository:** [`master-control-dashboard`](https://github.com/flynn33/Master-Control-Orchestration-Server)
- **Current release:** `v0.2.11` (2026-04-12)
- **Forsetti modules:** 19

---

## Why this project exists

Modern AI orchestration usually means stitching together a half-dozen CLI tools, 
duct-taping JSON config across them, and praying nothing rotates. This project 
collapses that into one Windows-native control plane: install once, point your 
providers and sub-agents at it, and run everything from a single Tron-styled shell 
or browser dashboard.

### Highlights

| | |
| --- | --- |
| **Single-binary control plane** | Windows service host, WinUI 3 desktop shell, and browser admin UI — all backed by the same in-process runtime |
| **Auto-Connect AI providers** | Enter credentials, pick roles, runtime handles capability resolution, model discovery, DPAPI encryption, and assignment fan-out |
| **Live command stream** | Every admin API request is captured by a 512-event ring buffer with millisecond timestamps, methods, targets, status codes, and latency |
| **CLU governance** | First-class Forsetti service module for posture, rules, role routing, Apple operations, and platform governance execution |
| **Cross-platform gateways** | Windows, macOS, and iOS gateway + governance lanes — Apple lanes route through SSH/companion-service Apple hosts |
| **Repo-owned installer** | Native setup launcher with Tron progress UI, plus diagnostic PowerShell fallback, plus bootstrapper for preflight/install/validate/upgrade/repair/uninstall |
| **Tron aesthetic, end-to-end** | Cyan-on-blue-black palette, Bahnschrift SemiCondensed type, zero corner radii, accent pulse animations, focus-visible outlines, prefers-reduced-motion respected |

---

## Repository layout

```
master-control-dashboard/
├── include/MasterControl/         # public contracts, models, defaults
├── src/
│   ├── MasterControlApp/          # shared runtime (~9k LOC core)
│   ├── MasterControlServiceHost/  # Windows service entry point
│   ├── MasterControlShell/        # WinUI 3 operator shell
│   ├── MasterControlModules/      # Forsetti modules + JSON manifests
│   └── MasterControlBootstrapper/ # installer / setup launcher
├── resources/
│   ├── web/                       # browser admin UI assets
│   └── clu/                       # CLU governance profile
├── scripts/                       # build, package, deploy, agents
├── plans/                         # design + infrastructure notes
├── docs/wiki/                     # wiki source pages (auto-generated)
└── docs/versions/                 # release docs (auto-generated)
```

---

## Build, validate, and stage

```powershell
# Configure and build (Debug)
cmake --preset debug
cmake --build build\debug --config Debug

# Run the local test suite
ctest --test-dir build\debug -C Debug --output-on-failure

# Forsetti compliance + repo native checks
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-mastercontrol-forsetti.ps1

# Stage installable payload
cmake --install build\debug --config Debug --prefix dist\debug

# Build a signed release package
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Package-MasterControlOrchestrationServer.ps1 -Preset release
```

See [Operations](docs/wiki/Operations.md) for the full deployment matrix, and 
[Architecture](docs/wiki/Architecture.md) for the runtime composition diagram.

---

## Documentation

| Page | What you get |
| --- | --- |
| [Home](docs/wiki/Home.md) | Project overview, current release, and navigation |
| [Architecture](docs/wiki/Architecture.md) | Runtime composition, Forsetti modules, request flow diagrams |
| [API Reference](docs/wiki/API-Reference.md) | Every admin API route with method, payload, and example responses |
| [Auto-Connect AI](docs/wiki/Auto-Connect-AI.md) | The end-to-end automation pipeline for adding AI providers |
| [CLU Governance](docs/wiki/CLU-Governance.md) | Command Logic Unit module, rules, roles, and platform governance lanes |
| [Telemetry & Activity](docs/wiki/Telemetry-and-Activity.md) | Live telemetry pipeline + the activity ring buffer |
| [Tron UI Theme](docs/wiki/Tron-UI-Theme.md) | Palette, typography, motion language, and component recipes |
| [Sub-Agents](docs/wiki/Sub-Agents.md) | The 7-agent roster, ports, and shared platform gateway client |
| [Operations](docs/wiki/Operations.md) | Build, package, install, upgrade, repair, uninstall flows |
| [Infrastructure](docs/wiki/Infrastructure.md) | Deployment shape, packaging model, and target hosts |
| [Remote Client](docs/wiki/Remote-Client.md) | Onboarding direction for Codex, Claude Code, and gateway discovery |
| [Automation](docs/wiki/Automation.md) | The GitHub agents that maintain the repository |
| [Versions](docs/wiki/Versions.md) | Release history and the versioning scheme |
| [Troubleshooting](docs/wiki/Troubleshooting.md) | Common failure modes and how to diagnose them |

---

## Current release

**`v0.2.11` — 2026-04-12**

Automated patch release for Master Control Orchestration Server.

- fix(shell): freeze Auto-Connect card during user interaction (flynn33)

---

Repository: https://github.com/flynn33/Master-Control-Orchestration-Server
