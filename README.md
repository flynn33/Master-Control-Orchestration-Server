# Master Control Orchestration Server

Master Control Orchestration Server is a Forsetti-compliant Windows orchestration control plane for guided setup, governance, MCP services, sub-agents, AI provider routing, telemetry, and browser-based operations.

- Repository slug: `master-control-dashboard`
- Repository URL: https://github.com/flynn33/master-control-dashboard

## Current Release

- Version: `v0.1.56`
- Release date: `2026-04-03`
- Summary: Automated patch release for Master Control Orchestration Server.

## Highlights

- Windows Service host, WinUI 3 operator shell, and browser admin surface backed by the same local runtime.
- Guided setup workflows for providers, MCP servers, sub-agents, Apple hosts, imports, and assignment flows.
- Command Logic Unit (CLU) Forsetti module for governance, responsibility routing, and platform-governance execution.
- CLU governance, platform gateway lanes, platform governance lanes, and Apple remote-host execution support.
- Release packaging, setup launcher, bootstrapper validation, and deployment acceptance tooling in-repo.

## Repository Layout

- `src/` - application hosts and shared runtime implementation.
- `include/` - shared contracts, models, and defaults.
- `resources/` - web assets and Forsetti manifests.
- `plans/` - current architecture and infrastructure notes.
- `docs/wiki/` - wiki source pages maintained by repository automation.
- `docs/versions/` - generated release documentation.

## Build And Validate

```powershell
cmake --build build\debug --config Debug
ctest --test-dir build\debug -C Debug --output-on-failure
cmake --install build\debug --config Debug --prefix dist\debug
```

## GitHub Agents

| Agent | Responsibility | Trigger |
| --- | --- | --- |
| Changelog Agent | Updates `CHANGELOG.md` after pushes to `main`. | `push`, `workflow_dispatch` |
| Wiki + README Agent | Regenerates `README.md`, wiki source pages, and syncs the GitHub wiki. | `push`, `workflow_dispatch` |
| AI Contributor Guard | Rejects commits that declare AI contributors and can block pushes locally. | `pre-push`, `push`, `pull_request` |
| Version Agent | Tracks semantic versions and creates the next numbered release. | `push`, `workflow_dispatch` |
| Version Documentation Agent | Generates release pages in `docs/versions/` and publishes GitHub releases. | `push`, `workflow_dispatch` |

## Key Project Notes

### Product Overview
- Forsetti-compliant Windows orchestration server for MCP services, AI coding agents, provider routing, CLU governance, imports, exports, and operator control.
- The product ships as a Windows service, a WinUI 3 desktop shell, and a browser-based admin surface backed by the same local runtime.
- make setup fast through guided workflows instead of low-level manual editing
- host and govern MCP servers, providers, sub-agents, and platform governance lanes from one control plane
- provide a desktop-first and browser-accessible operations surface for telemetry, runtime control, and deployment visibility
- package the product so it can be installed, validated, upgraded, repaired, and uninstalled with repo-owned tooling
- Windows service host for orchestration, telemetry, configuration, imports, governance, and browser APIs
- WinUI 3 shell for guided setup, CLU control, runtime operations, provider configuration, and security posture

### Technical Snapshot
- `src/MasterControlApp` hosts the shared application runtime, configuration, governance, Apple execution, and browser APIs
- `src/MasterControlModules` provides Forsetti modules and manifests
- `src/MasterControlServiceHost` runs the orchestration runtime as a Windows service or console host
- `src/MasterControlShell` provides the WinUI 3 operator shell
- `src/MasterControlBootstrapper` owns install, preflight, validate, repair, upgrade, and uninstall flows
- `DashboardUIModule` remains the single UI host under Forsetti rules
- `CommandLogicUnitModule` is the orchestration and governance coordinator for the product
- the CLU manifest lives at `src/MasterControlModules/Resources/ForsettiManifests/CommandLogicUnitModule.json`

### Command Logic Unit (CLU)
- CLU is a first-class Forsetti service module, not just a navigation label in the shell.
- Manifest: `src/MasterControlModules/Resources/ForsettiManifests/CommandLogicUnitModule.json`
- Module ID: `com.mastercontrol.command-logic-unit`
- Responsibilities: governance posture, rule evaluation, provider responsibility routing, Apple operations, and platform governance execution.

## Primary Documents

- [Project Overview](plans/dashboard/project-overview.md)
- [Technical Details](plans/dashboard/technical-details.md)
- [Remote Client Onboarding](plans/dashboard/remote-client-onboarding.md)
- [Deployment Overview](plans/infrastructure/deployment-overview.md)
- [Platform Gateway And Governance](plans/infrastructure/platform-gateway-and-governance.md)
- [Version Index](docs/versions/index.md)
- [Latest Release Notes](docs/versions/latest.md)

Repository URL: https://github.com/flynn33/master-control-dashboard
