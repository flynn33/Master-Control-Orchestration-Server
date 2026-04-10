# Master Control Orchestration Server Wiki

Master Control Orchestration Server is a Forsetti-compliant Windows orchestration server for guided setup, provider routing, CLU governance, platform gateways, platform governance lanes, telemetry, imports, exports, and browser-based operations.

## Current Release

| Field | Value |
| --- | --- |
| Version | `v0.1.61` |
| Released | `2026-04-10` |
| Summary | Automated patch release for Master Control Orchestration Server. |

## Platform at a Glance

| Component | Count | Details |
| --- | --- | --- |
| Operator surfaces | 2 | WinUI 3 desktop shell and browser admin UI backed by the same runtime |
| Platform lanes | 6 | Windows, macOS, and iOS gateway plus governance lanes |
| AI providers | 3 | Codex, Claude Code, and xAI routing in the current build |
| Install flow | 2 | Native setup launcher plus diagnostic PowerShell fallback |
| Governance | 1 core module | Command Logic Unit (CLU) plus platform governance execution and resource enforcement |

## Command Logic Unit

- CLU is a Forsetti service module with manifest ID `com.mastercontrol.command-logic-unit`.
- It coordinates governance posture, rule evaluation, model-to-responsibility routing, Apple operations, and platform governance execution.
- The shell and browser surfaces read CLU state from the runtime instead of duplicating governance logic locally.

## Wiki Pages

| Page | Description |
| --- | --- |
| [Architecture](Architecture) | Product composition, runtime structure, and platform governance model |
| [Infrastructure](Infrastructure) | Deployment shape, packaging model, and target-host validation focus |
| [Sub-Agents](Sub-Agents) | Current seven-agent roster, responsibilities, and shared client details |
| [API Reference](API-Reference) | Current browser, CLU, platform-service, and governance routes from the runtime |
| [Operations](Operations) | Build, validate, package, install, and deployment-acceptance workflows |
| [Remote Client](Remote-Client) | Current onboarding direction for Codex, Claude Code, and platform gateway discovery |
| [Automation](Automation) | GitHub agents, CI/CD pipeline, commit conventions, and workflow triggers |
| [Versions](Versions) | Release history, versioning scheme, and release documents |

## Quick Links

- [README](../README.md)
- [Project Overview](../plans/dashboard/project-overview.md)
- [Technical Details](../plans/dashboard/technical-details.md)
- [Version Index](../docs/versions/index.md)
- Repository: https://github.com/flynn33/master-control-dashboard
