# Packaging And Gateway Binary

This page explains what the current package contains and how gateway packaging
works. Source authority is `scripts/Package-MasterControlOrchestrationServer.ps1`,
`installer/Build-Msi.ps1`, and `installer/MasterControlOrchestrationServer.wxs`.

## Current Packaging Model

The MSI is the normal operator-facing installer. The zip artifact remains useful
for CI and headless staging, but a zip-only bundle is not equivalent to a normal
installer release unless explicitly requested with `-AllowZipOnly`.

Package command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Package-MasterControlOrchestrationServer.ps1 -Preset release
```

Current package name for `A3.11.0`:

```text
MasterControlOrchestrationServer-vA3.11.0-win-x64
```

## What The MSI Installs

| Payload | Purpose |
|---|---|
| `MasterControlServiceHost.exe` | Windows service and console host. |
| `MasterControlBootstrapper.exe` | Preflight, install, uninstall, and validation helper used by custom actions. |
| `MasterControlOrchestrationServer.exe` | Launcher for the local Windows app. |
| `MasterControlShell.exe` | WinUI 3 desktop shell. |
| `share\MasterControlOrchestrationServer\web\` | Browser dashboard assets. |
| `share\MasterControlOrchestrationServer\ForsettiManifests\` | Vendored Forsetti manifests. |
| `share\claude-plugins\mcos-control\` | Local bridge plugin source. |
| `Register-McosControlPlugin.ps1` | Helper for user-side plugin registration. |
| VC++ and Windows App SDK runtime files | Runtime dependencies staged by the package script. |

## Installer Options

`installer/MasterControlOrchestrationServer.wxs` defines these option
properties:

| Property | Default | Meaning |
|---|---:|---|
| `INSTALL_SERVICE` | `1` | Register and start the Windows service. |
| `INSTALL_FIREWALL` | `1` | Run bootstrapper firewall setup. |
| `INSTALL_START_MENU_SHORTCUT` | `1` | Create Start Menu shortcut. |
| `INSTALL_DESKTOP_SHORTCUT` | `1` | Create Desktop shortcut. |
| `LAUNCH_ON_EXIT` | `1` | Launch the Windows app after install. |

There is no MSI checkbox for bridge plugin registration. The MSI runs elevated;
plugin registration is a per-user action through the dashboard toggle or
`Register-McosControlPlugin.ps1`.

## Gateway Binary Status

No external MCP gateway binary is required or supported in the current alpha.
The shipping substrate is the in-process `NativeHttpSysGatewayAdapter` inside
`MasterControlServiceHost.exe`.

Legacy fields such as `mcpGateway.binaryPath` and `mcpGateway.databasePath`
remain in JSON for backward-compatible round-trips, but the runtime does not
require a separate gateway executable.

## Version Mapping

`VERSION.json` owns the product version. `installer/Build-Msi.ps1` maps
alpha-stage versions to Windows Installer ProductVersion:

| MCOS version | MSI ProductVersion |
|---|---|
| `A3.11.0` | `3.11.0.0` |

## Post-Install Checks

```powershell
& "C:\Program Files\Master Control Orchestration Server\MasterControlBootstrapper.exe" preflight --json-output
Get-Service MasterControlProgram
Invoke-RestMethod http://localhost:7300/api/health | ConvertTo-Json
Invoke-RestMethod http://localhost:7300/api/gateway/status | ConvertTo-Json -Depth 4
```

## Related Pages

[Quick Start](Quick-Start) |
[Release Gate](Release-Gate) |
[Gateway](Gateway) |
[Windows Firewall and LAN Mode](Windows-Firewall-LAN-Mode) |
[Maintenance](Maintenance)
