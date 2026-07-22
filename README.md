# Master Control Orchestration Server

![version](https://img.shields.io/badge/version-vA3.12.0-00f6ff?style=flat-square)
![channel](https://img.shields.io/badge/channel-Internal%20Alpha-ff8c00?style=flat-square)
![status](https://img.shields.io/badge/status-implementation%20milestone-ff8c00?style=flat-square)
![platform](https://img.shields.io/badge/platform-Windows%2011%20%2F%20Server%202022-0a1018?style=flat-square)
![architecture](https://img.shields.io/badge/architecture-LAN%20MCP%20Gateway%20Host-1cf2c1?style=flat-square)
![license](https://img.shields.io/badge/license-Apache%202.0-031018?style=flat-square)

Master Control Orchestration Server is a Windows-native LAN MCP Gateway host.
It exposes one MCOS-advertised MCP endpoint for trusted-LAN clients, supervises
MCP server and sub-agent worker pools, distributes onboarding profiles and
CLU/Forsetti governance bundles, and provides browser plus WinUI maintainer
surfaces for operations.

> **vA3.12.0** — Model Parity (implementation milestone; not yet released)

Current version: `vA3.12.0` — **Model Parity**, an implementation milestone that
is not yet released (no tag cut, no MSI packaged). Last released package:
`vA3.11.0`, released `2026-07-03`.
`A3.11.0` is the alpha-stage re-expression of the tree previously documented as
`0.11.0-alpha.3`. During alpha, versions use
`A{alphaIteration}.{feature}.{patch}`. Alpha versions are published as GitHub
pre-releases with the MSI installer; the Windows Build, Test, and Package
workflow is the repository health gate.
`VERSION.json` is the version authority.

The product gate proves build, test, package creation, and staged bootstrapper
preflight only. Without operator-authorized target-host Gate D and second-host
Gate E evidence, the honest status is **Alpha candidate ready for operator
deployment certification**.

## Model Parity

Model Parity (`A3.12.0`) is the current implementation milestone. It adds a
provider-neutral **Client Integration Catalog**: MCOS supports a fixed set of
external AI client and model surfaces through the governed MCP Gateway and
provider-native onboarding artifacts. This is an implementation milestone landed
in the current tree, not a released or deployed feature.

The catalog registers ten canonical integrations:

- `claude-code` — Claude Code / Desktop (Anthropic), `.mcp.json`.
- `codex` — Codex CLI / IDE (OpenAI), `config.toml`.
- `codex-mcp-server` — Codex as an external MCP server (stdio adapter).
- `openai-responses` — OpenAI Responses API remote MCP (public HTTPS).
- `chatgpt-apps` — ChatGPT Apps / Connectors (public HTTPS, OAuth 2.1).
- `chatgpt-connector-edge` — LAN-to-public connector edge bridge.
- `xai-responses` — xAI Responses / Grok API remote MCP.
- `grok-build` — Grok Build CLI / headless (xAI), `.grok/config.toml`.
- `grok-build-acp` — Grok Build ACP (JSON-RPC stdio adapter).
- `generic-mcp` — any MCP-compliant client.

The gateway remains the POST-only Streamable HTTP subset with no SSE; artifacts
carry only placeholder secrets. See
[Client Integrations](docs/wiki/Client-Integrations.md) for per-provider details,
the compatibility matrix, and the `/api/client-integrations` routes.

## Current Alpha State

Implemented in the current tree:

- Native in-process HTTP.sys MCP gateway behind `IMcpGateway`.
- Trusted-LAN discovery through DNS-SD/mDNS, UDP beacon, `/api/discovery`, and
  `/.well-known/mcos.json`.
- Per-client onboarding profiles for Claude Code, Codex, Grok, ChatGPT
  connector-edge, and generic MCP clients.
- CLU/Forsetti governance bundle distribution and approval workflows.
- Managed endpoint pools for MCP servers and sub-agents, with Job Object process
  containment, lease routing, autoscaling policy, and sticky session behavior.
- Diagnostics routes, persistent diagnostics store with JSONL fallback, browser
  diagnostics tab, WinUI diagnostics section, and bridge diagnostics tools.
- Optional gateway TLS through HTTP.sys certificate binding and optional admin
  listener TLS through SChannel.
- MSI packaging through WiX plus a zip artifact for CI/headless use.

Current limitations:

- MCOS is internal alpha software. Operator-authorized Windows host validation
  remains required for HTTP.sys binding, TLS binding, MSI installation, service
  behavior, DNS-SD visibility, and live LAN-client interoperability on each
  target environment before any deployment-qualified or certified working
  internal alpha claim.
- LAN client authentication is intentionally network-level during alpha:
  `auth=none, trust=lan`. App-layer authentication is deferred to a later
  hardening track.
- The native gateway implements the POST subset of MCP Streamable HTTP. Clients
  that require a server-initiated SSE MCP stream need a client-side bridge or a
  later transport expansion.
- Some validation paths depend on host toolchain availability. In particular,
  the WinUI Shell project requires the configured Visual Studio toolset.

## Quick Links

- Live wiki: [Master-Control-Orchestration-Server wiki](https://github.com/flynn33/Master-Control-Orchestration-Server/wiki)
- Wiki source: [docs/wiki](docs/wiki)
- Quick Start: [docs/wiki/Quick-Start.md](docs/wiki/Quick-Start.md)
- Configuration: [docs/wiki/Configuration.md](docs/wiki/Configuration.md)
- TLS and HTTPS: [docs/wiki/TLS-and-HTTPS.md](docs/wiki/TLS-and-HTTPS.md)
- Gateway: [docs/wiki/Gateway.md](docs/wiki/Gateway.md)
- Worker Pools: [docs/wiki/Worker-Pools.md](docs/wiki/Worker-Pools.md)
- API Reference: [docs/wiki/API-Reference.md](docs/wiki/API-Reference.md)
- Troubleshooting: [docs/wiki/Troubleshooting.md](docs/wiki/Troubleshooting.md)
- Versions: [docs/wiki/Versions.md](docs/wiki/Versions.md)
- Changelog: [CHANGELOG.md](CHANGELOG.md)
- Version authority: [VERSION.json](VERSION.json)

## Build, Validate, Package

From a Windows developer shell with the repository checked out:

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-mastercontrol-forsetti.ps1
```

Release package flow:

```powershell
cmake --preset release
cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure --timeout 300
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Package-MasterControlOrchestrationServer.ps1 -Preset release -SkipBuild
```

The package script reads `VERSION.json`. For `A3.11.0`, the MSI ProductVersion
mapping is `3.11.0.0` per `installer/Build-Msi.ps1`.

## Install And Smoke Test

After packaging, install the MSI from the release package directory:

```powershell
msiexec /i "dist\packages\release\MasterControlOrchestrationServer-vA3.11.0-win-x64\MasterControlOrchestrationServer-vA3.11.0-win-x64.msi" /l*v "$env:TEMP\mcos-install.log"
```

Basic local checks:

```powershell
& "C:\Program Files\Master Control Orchestration Server\MasterControlBootstrapper.exe" preflight --json
Invoke-RestMethod http://localhost:7300/api/health | ConvertTo-Json
Invoke-RestMethod http://localhost:7300/api/discovery | ConvertTo-Json -Depth 6
Invoke-RestMethod http://localhost:7300/api/gateway/status | ConvertTo-Json -Depth 4
```

The current configuration file is:

```text
%ProgramData%\MasterControlOrchestrationServer\config\master-control-orchestration-server.json
```

The legacy fallback name is:

```text
%ProgramData%\MasterControlOrchestrationServer\config\master-control-program.json
```

See [Configuration](docs/wiki/Configuration.md) before editing the file
directly. Prefer `PATCH /api/config` for partial changes and reserve
`POST /api/config` for full-document replacement.

## Repository Layout

```text
include/MasterControl/             Public C++ contracts, models, defaults
src/MasterControlApp/              Runtime core and gateway adapters
src/MasterControlBootstrapper/     Installer and lifecycle helper
src/MasterControlServiceHost/      Windows service and console host
src/MasterControlShell/            WinUI 3 desktop shell
src/MasterControlModules/          MCOS module registrations
resources/web/                     Browser dashboard
resources/clu/                     Governance profile JSON
installer/                         WiX MSI source
scripts/                           Existing build, package, cert, validation helpers
tests/                             C++ test suite
docs/wiki/                         Canonical source for the live GitHub wiki
docs/implementation/               Architecture contracts and schemas
docs/archive/                      Historical evidence only
handoff/realignment/               Active realignment manifest and phase material
Forsetti-Framework-Windows-main/   Vendored Forsetti framework
```

Archived documents are historical evidence only. Current operator guidance lives
in `docs/wiki/`; current release metadata lives in `VERSION.json` and
`CHANGELOG.md`.

## Contributing

This is a proprietary repository. Keep changes inside the active scope, preserve
Forsetti boundaries, and keep product claims tied to source, tests, scripts,
installer definitions, `VERSION.json`, or retained historical release notes.

## License

Copyright 2026 James Daley

This project is licensed under the Apache License, Version 2.0.
See the [LICENSE](LICENSE) file for the full terms.
