# Master Control Orchestration Server Operator Wiki

![version](https://img.shields.io/badge/version-vA3.11.0-00f6ff?style=flat-square)

This wiki is the operator manual for MCOS, a Windows-native LAN MCP Gateway
host. Use it to install the service, configure the gateway, connect trusted-LAN
clients, operate worker pools, review diagnostics, and troubleshoot the host.

## Current Alpha

| Field | Value |
|---|---|
| Version | `vA3.11.0` |
| Version source | `VERSION.json` |
| Released | `2026-07-03` |
| Channel | Internal alpha |
| Version meaning | `A3.11.0` is the alpha-stage expression of the tree previously documented as `0.11.0-alpha.3`. |
| Release policy | Alpha versions are published as GitHub pre-releases with the MSI installer; the Windows Build, Test, and Package workflow is the repository health gate. |
| Gateway substrate | Native in-process HTTP.sys behind `IMcpGateway`; no external gateway binary is required. |
| Trust posture | `auth=none, trust=lan`; app-layer authentication is deferred to a later hardening track. |

MCOS is internal alpha software. Repository checks and tests validate the source
tree, but each target Windows host still needs environment-specific validation
for HTTP.sys binding, TLS binding, MSI installation, service behavior, DNS-SD
visibility, and live LAN-client interoperability.

Archived documents are historical evidence only. They are not current
installation, operation, release, or implementation guidance.

## Set Up

| Task | Page |
|---|---|
| Install or build from source | [Quick Start](Quick-Start) |
| Configure ports, identity, gateway, security, and resource fields | [Configuration](Configuration) |
| Enable gateway or admin TLS | [TLS and HTTPS](TLS-and-HTTPS) |
| Open Windows Firewall for LAN use | [Windows Firewall and LAN Mode](Windows-Firewall-LAN-Mode) |
| Understand MSI and package artifacts | [Packaging and Gateway Binary](Packaging-and-Gateway-Binary) |

## Connect Clients

| Task | Page |
|---|---|
| Generate per-client onboarding profiles | [Onboarding](Onboarding) |
| Inspect client bundle fields | [Client Config Bundle](Client-Config-Bundle) |
| Verify DNS-SD, UDP beacon, and discovery documents | [LAN Discovery](LAN-Discovery) |
| Manage LAN client identity and capabilities | [LAN Clients](LAN-Clients) |
| Connect a remote trusted-LAN peer | [Remote Client](Remote-Client) |
| Use the local bridge plugin | [Claude Code Plugin](Claude-Code-Plugin) |

## Operate

| Task | Page |
|---|---|
| Run daily health checks | [Daily Operations](Daily-Operations) |
| Use the browser dashboard and WinUI shell | [Dashboard](Dashboard) |
| Interpret activity, diagnostics, and unavailable metrics | [Telemetry and Activity](Telemetry-and-Activity) |
| Start, stop, and inspect the MCP gateway | [Gateway](Gateway) |
| Add, drain, scale, and remove worker pools | [Worker Pools](Worker-Pools) |
| Understand sub-agent behavior | [Sub-Agents](Sub-Agents) |
| Review CLU/Forsetti governance | [Governance](Governance) and [CLU Governance](CLU-Governance) |
| Grant least-privilege capabilities | [Privileges](Privileges) |

## Maintain And Troubleshoot

| Task | Page |
|---|---|
| Back up, restore, repair, upgrade, or uninstall | [Maintenance](Maintenance) |
| Run build, test, package, and local operations commands | [Operations](Operations) |
| Understand alpha release gates | [Release Gate](Release-Gate) |
| Validate an installed host with non-destructive runtime probes | [Deployment Acceptance](Deployment-Acceptance) |
| Review service, ProgramData, HTTP.sys, firewall, and discovery infrastructure | [Infrastructure](Infrastructure) |
| Inspect existing automation surfaces | [Automation](Automation) |
| Diagnose common failures | [Troubleshooting](Troubleshooting) |

## Reference

| Topic | Page |
|---|---|
| Runtime architecture | [Architecture](Architecture) |
| ADR index | [Architecture Decisions](Architecture-Decisions) |
| LAN client control-plane decision | [ADR-001](ADR-001-lan-client-control-plane) |
| Gateway-first MCP decision | [ADR-002](ADR-002-gateway-first-mcp-realignment) |
| Gateway substrate decision | [ADR-003](ADR-003-mcp-gateway-substrate-decision) |
| Admin API routes and capability requirements | [API Reference](API-Reference) |
| Version scheme and release history | [Versions](Versions) |
| UI theme notes | [Tron UI Theme](Tron-UI-Theme) |

## Source Notes

- Version authority: `VERSION.json`
- Route registry: `include/MasterControl/AdminRouteRegistry.h`
- Capability policy: `include/MasterControl/AdminRouteAuthorization.h`
- Configuration defaults and paths: `src/MasterControlApp/MasterControlDefaults.cpp`
- Gateway adapter: `src/MasterControlApp/McpGatewayAdapters.cpp`
- Packaging: `scripts/Package-MasterControlOrchestrationServer.ps1` and `installer/Build-Msi.ps1`

## Related Pages

[Quick Start](Quick-Start) |
[Configuration](Configuration) |
[TLS and HTTPS](TLS-and-HTTPS) |
[Gateway](Gateway) |
[Worker Pools](Worker-Pools) |
[API Reference](API-Reference) |
[Troubleshooting](Troubleshooting) |
[Versions](Versions)
