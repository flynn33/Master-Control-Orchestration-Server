# Master Control Orchestration Server

![version](https://img.shields.io/badge/version-v0.4.2--rc.8-00f6ff?style=flat-square) ![released](https://img.shields.io/badge/released-2026--04--17-031018?style=flat-square) ![platform](https://img.shields.io/badge/platform-Windows%2011%20/%20Server%202022-0a1018?style=flat-square) ![toolchain](https://img.shields.io/badge/toolchain-C++20%20·%20WinUI%203%20·%20CMake-00aacc?style=flat-square) ![license](https://img.shields.io/badge/license-Proprietary-5a00e8?style=flat-square)

> Forsetti-compliant Windows orchestration control plane for MCP services, AI provider routing, 
> CLU governance, sub-agents, platform gateways, telemetry, and browser-based operations — 
> all delivered as a single Tron-themed product.

- **Repository:** [`master-control-dashboard`](https://github.com/flynn33/Master-Control-Orchestration-Server)
- **Current release:** `v0.4.2-rc.8` (2026-04-17)
- **Forsetti modules:** 19

---

## Why this project exists

This project provides a Windows-native orchestration control plane for managing AI 
providers, MCP services, sub-agents, and platform gateways on an internal LAN. It 
brings together service hosting, a desktop operator shell, and a browser admin 
surface — all backed by one shared in-process runtime.

**Target audience:** Developers and operators running AI orchestration infrastructure 
on a secure internal network. This is not a consumer-ready one-click product — it 
requires a Windows build toolchain (Visual Studio 2022+, CMake 3.28+, vcpkg) or 
the bootstrapper-assisted install path.

### Architecture

The product consists of multiple binaries sharing a single runtime library:

- **MasterControlServiceHost** — Windows service entry point (background daemon)
- **MasterControlShell** — WinUI 3 desktop operator shell (local UI)
- **Browser surface** — Vanilla JS admin dashboard served by the service host (remote access)
- **MasterControlBootstrapper** — Installer/upgrader/repair tool with Tron progress UI

### Highlights

| | |
| --- | --- |
| **Multi-binary control plane** | Windows service host, WinUI 3 desktop shell, and browser admin UI — all backed by the same in-process runtime |
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

**`v0.4.2-rc.8` — 2026-04-17**

UX overhaul: guided AI-model path surfaces first, advanced orchestration config hidden behind Expanders, symmetric button sizes with destructive-tone Remove controls, Run Provider Task shows a live progress ring, and successful installs stop littering the desktop with log receipts (failures still write). Jargon sweep: "Forsetti Surface Toolbar" → "Quick Actions", "Protection Envelope" → "Security Settings", "Governed Resource Envelope" → "Resource Allocation".

- fix(installer): MasterControlBootstrapper and setup launcher no longer write desktop log receipts on successful installs. Failures still land on the desktop; every run persists in %PUBLIC%\Documents\Master Control Orchestration Server\logs\installer regardless of outcome.
- feat(shell): Providers section now presents a guided primary path (sign-in cards + Auto-Connect + Provider Connections list + Provider Modules). Provider Editor, Credentials, AI Autonomy, Sub-Agent Groups, Ownership Routing, and Execution Console are collapsed behind two Advanced Expanders so first-time operators are no longer overwhelmed.
- fix(shell): unified button sizing across implicit Button and ShellCommandButtonStyle (MinHeight=48, Padding=12,10, HorizontalAlignment=Stretch); ShellSecondaryButtonStyle is now visibly recessive. Remove Group uses ShellDangerButtonStyle. Paired action rows wrapped in equal-width Grids so Save / New / Remove widths match.
- feat(shell): Run Provider Task shows a live ProgressRing beside the button while the admin API call is in flight.
- fix(shell/browser): jargon sweep — "Forsetti Surface Toolbar" → "Quick Actions"; "Protection Envelope" → "Security Settings"; "Governed Resource Envelope" → "Resource Allocation" across MainWindow.xaml, SecuritySectionControl.xaml, SettingsSectionControl.xaml, TelemetrySectionControl.xaml, setup-wizard Step 3 label, and the browser index.html / app.js harden-security card.
---

Repository: https://github.com/flynn33/Master-Control-Orchestration-Server
