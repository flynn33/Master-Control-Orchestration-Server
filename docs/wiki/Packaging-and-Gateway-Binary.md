# Packaging and Gateway Binary

> **Note (post-v0.9.0):** The pre-v0.9.0 framing of this page stated that the MSI deliberately omits the gateway and maintainers must install a separate gateway binary. That is no longer true. The in-process `NativeHttpSysGatewayAdapter` ships inside `MasterControlServiceHost.exe` since v0.9.0. The installer-process history below is preserved for traceability.

PHASE-10 baseline (ADR-002 §10, §11).

## What the MSI installs

`installer/Build-Msi.ps1` produces `MasterControlOrchestrationServer-vX.Y.Z-win-x64.msi` from the staged release tree. Per `installer/MasterControlOrchestrationServer.wxs`, the MSI installs:

| Path under install dir | Contents |
|---|---|
| `MasterControlServiceHost.exe` | The orchestration runtime. Hosts the HTTP API, the gateway adapter, the worker supervisor, the lease router, the telemetry aggregator, and the discovery service. Runs as a Windows service or in `--console` mode. |
| `MasterControlBootstrapper.exe` | The setup binary. Runs preflight, install, uninstall, and diagnostics. Used by the MSI custom actions. |
| `MasterControlOrchestrationServer.exe` | Win32 thin launcher. Brings the WinUI shell to the foreground. |
| `MasterControlShell.exe` | The WinUI 3 desktop shell. |
| `share/MasterControlOrchestrationServer/web/` | Browser dashboard (`index.html`, `app.js`, `styles.css`, icons). |
| `share/MasterControlOrchestrationServer/ForsettiManifests/` | Vendored Forsetti manifest files. |
| `share/claude-plugins/mcos-control/` | The Claude Code plugin source (bridge MCP server, sub-agents, slash commands, skill, README). Maintainer registers with `Register-McosControlPlugin.ps1` after install. See [Claude Code Plugin](Claude-Code-Plugin). |
| `Register-McosControlPlugin.ps1` | Helper script at the install root that copies (or junctions, with `-Symlink`) the plugin into `%USERPROFILE%\.claude\plugins\mcos-control`. Idempotent; `-Unregister` removes it. |
| `resources/clu/governance-profile.json` | CLU governance profile (consumed at runtime by the governance bundle service). |
| Microsoft VC++ x64 runtime DLLs | Bundled from `Resolve-MasterControlToolchain.ps1`'s `VcRuntimeDirectory`. |
| Microsoft Windows App SDK runtime DLLs | Bundled with the WinUI shell payload. |

## Gateway substrate

The MCP gateway is `NativeHttpSysGatewayAdapter`, an in-process HTTP.sys listener built into `MasterControlServiceHost.exe`. No external gateway binary is required, maintainer-installed, supervised, or supported. the legacy external gateway is not installed by the MSI and is not used at runtime.

The `mcpGateway.type` field in `mcos.json` is retained for back-compat deserialization only; the runtime always activates the native adapter regardless of its value.

The gateway binds at `http://<lan-ip>:8080/mcp` by default (configurable via `mcpGateway.listenPort` and `mcpGateway.mcpPath`). The bootstrapper registers the HTTP.sys URL ACL (`netsh http add urlacl url=http://+:8080/ user=Everyone`) at install time so the adapter can bind without elevation in console mode.

## Post-install smoke test

The bootstrapper's `preflight --json-output` is the canonical smoke. Run it after install:

```powershell
& "C:\Program Files\Master Control Orchestration Server\MasterControlBootstrapper.exe" preflight --json-output
```

A successful preflight reports zero issues across all categories: install layout, registry hooks, runtime binary, governance profile, web assets, manifests, VC++ runtime, Windows App SDK runtime.

The CI release gate (`.github/workflows/windows-build-test-package.yml`, "Bootstrapper preflight validation" step) runs the same command against the staged package before the artifact is uploaded.

## Runtime smoke test

Console-mode start exercises the full HTTP surface without registering the Windows service:

```powershell
& "C:\Program Files\Master Control Orchestration Server\MasterControlServiceHost.exe" --console
```

Look for the line:

```
Master Control Orchestration Server listening at http://<host>:<admin-port>
```

Hit `/api/health` (maintainer surface) and `/api/gateway/status` (gateway surface). Ctrl+C to stop. The runtime reaps any supervised worker tree atomically via `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` on shutdown.

## Firewall scoping

After install, add the inbound rules from [Windows Firewall and LAN Mode](Windows-Firewall-LAN-Mode.md). The MSI does not own firewall policy.

## Uninstall

```powershell
msiexec /uninstall <productCode>
```

Uninstall removes binaries, web assets, manifests, runtime DLLs, registry hooks, and the start menu entries. It does **not** remove:

- `mcos.json` and any maintainer-edited configuration under `%ProgramData%\Master Control Orchestration Server\` (preserved across upgrades).
- Windows Firewall rules (see the Firewall doc for cleanup).

## Versioning

`VERSION.json` is the single source of truth. The MSI's `ProductVersion` is derived from `current_version` via `installer/Build-Msi.ps1`'s `ConvertTo-MsiProductVersion` (e.g., `0.6.0` → `0.6.0.0`; `0.6.0-rc.3` → `0.6.0.3`). `Sync-RepositoryVersionBadges.ps1` keeps `README.md` badges and `vcpkg.json` `version-string` aligned.

PHASE-10 lifted the `noBumpUntilPhaseTen: true` rule from `handoff/realignment/manifest.json`, so this is the first phase that may bump `VERSION.json`.

## See also

- [Windows Firewall and LAN Mode](Windows-Firewall-LAN-Mode.md)
- [Release Gate](Release-Gate.md)
- [ADR-002](ADR-002-gateway-first-mcp-realignment.md) — sections 10 (CI gate), 11 (vendoring)
