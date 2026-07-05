# Quick Start

This page gets MCOS built or installed on one Windows host, verifies the local
admin surface, and checks LAN discovery. MCOS is internal alpha software; run
the verification commands on every target host.

## Requirements

| Requirement | Why |
|---|---|
| Windows 11 or Windows Server 2022 | MCOS is a Windows-native C++20 application. |
| Visual Studio C++ toolchain, CMake, vcpkg | Required for source builds. |
| WiX v5 global tool | Required for MSI packaging. |
| Administrative privileges | Required for MSI install, Windows service, firewall, and certificate binding. |
| Trusted LAN peer | Required to verify DNS-SD and client reachability. |

## Build From Source

```powershell
cmake --preset release
cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure --timeout 300
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Package-MasterControlOrchestrationServer.ps1 -Preset release -SkipBuild
```

This proves the source build, tests, package creation, and staged bootstrapper
preflight for the local tree. It does not make the host deployment-qualified;
that requires the Gate D/E evidence path in [Deployment Acceptance](Deployment-Acceptance).

The package script reads `VERSION.json`. For the current alpha, the MSI path is:

```text
dist\packages\release\MasterControlOrchestrationServer-vA3.11.0-win-x64\MasterControlOrchestrationServer-vA3.11.0-win-x64.msi
```

If WiX is unavailable, `-AllowZipOnly` can produce a zip bundle, but the MSI is
the normal operator-facing installer.

## Install

Interactive install with logging:

```powershell
msiexec /i "dist\packages\release\MasterControlOrchestrationServer-vA3.11.0-win-x64\MasterControlOrchestrationServer-vA3.11.0-win-x64.msi" /l*v "$env:TEMP\mcos-install.log"
```

Silent install:

```powershell
msiexec /i "dist\packages\release\MasterControlOrchestrationServer-vA3.11.0-win-x64\MasterControlOrchestrationServer-vA3.11.0-win-x64.msi" /qn /l*v "$env:TEMP\mcos-install.log"
```

MSI option properties from `installer/MasterControlOrchestrationServer.wxs`:

| Property | Default | Meaning |
|---|---:|---|
| `INSTALL_SERVICE` | `1` | Register and start `MasterControlProgram`. |
| `INSTALL_FIREWALL` | `1` | Configure Windows Firewall rules for LAN admin UI through the bootstrapper. |
| `INSTALL_START_MENU_SHORTCUT` | `1` | Create Start Menu shortcut. |
| `INSTALL_DESKTOP_SHORTCUT` | `1` | Create Desktop shortcut. |
| `LAUNCH_ON_EXIT` | `1` | Launch the Windows app after installation. |

The MSI does not provide a plugin-registration checkbox. Use the dashboard
toggle or `scripts\Register-McosControlPlugin.ps1` after install.

## First Local Checks

```powershell
Get-Service MasterControlProgram

& "C:\Program Files\Master Control Orchestration Server\MasterControlBootstrapper.exe" preflight --json

Invoke-RestMethod http://localhost:7300/api/health | ConvertTo-Json
Invoke-RestMethod http://localhost:7300/api/discovery | ConvertTo-Json -Depth 6
Invoke-RestMethod http://localhost:7300/api/gateway/status | ConvertTo-Json -Depth 4
```

For operator evidence, run the non-destructive deployed-runtime probe:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\Test-MasterControlOrchestrationServerDeployedRuntime.ps1 `
    -BaseUrl http://localhost:7300 `
    -OutputDirectory artifacts\deployability-audit\gate-d `
    -Strict
```

Default current configuration path:

```text
%ProgramData%\MasterControlOrchestrationServer\config\master-control-orchestration-server.json
```

## Enable Trusted-LAN Gateway Access

Fresh installs default to local-only. To enable the native gateway on all
interfaces for a trusted LAN:

```powershell
$patch = @{
  mcpGateway = @{
    enabled = $true
    listenHost = "0.0.0.0"
    mode = "trusted-lan"
  }
  security = @{
    allowOpenLanAccess = $true
    securityPosture = "trusted-lan"
  }
}

Invoke-RestMethod http://localhost:7300/api/config `
  -Method Patch `
  -ContentType "application/json" `
  -Headers @{ "X-Confirm-Unsafe" = "1" } `
  -Body ($patch | ConvertTo-Json -Depth 8)

Restart-Service MasterControlProgram
Invoke-RestMethod http://localhost:7300/api/gateway/start -Method Post
```

Verify the gateway:

```powershell
Invoke-RestMethod http://localhost:8080/health | ConvertTo-Json

$init = @{
  jsonrpc = "2.0"
  id = 1
  method = "initialize"
  params = @{
    protocolVersion = "2025-03-26"
    capabilities = @{}
    clientInfo = @{ name = "smoke"; version = "1.0" }
  }
}

Invoke-RestMethod http://localhost:8080/mcp `
  -Method Post `
  -ContentType "application/json" `
  -Body ($init | ConvertTo-Json -Depth 8)
```

## Verify LAN Discovery

From a second host on the same LAN:

```powershell
Resolve-DnsName -Name _mcos._tcp.local -Type PTR -LlmnrFallback
Invoke-RestMethod http://<mcos-host>:7300/.well-known/mcos.json | ConvertTo-Json -Depth 6
```

macOS:

```bash
dns-sd -B _mcos._tcp
```

Linux with Avahi:

```bash
avahi-browse _mcos._tcp
```

If discovery fails, check [Windows Firewall and LAN Mode](Windows-Firewall-LAN-Mode)
and [Troubleshooting](Troubleshooting).

## Optional HTTPS

Gateway HTTPS and admin listener TLS are separate. Start with
[TLS and HTTPS](TLS-and-HTTPS) before enabling either:

```powershell
scripts\Configure-LocalServerCert.ps1 -RestartService
```

Then patch `mcpGateway.tlsEnabled`, `mcpGateway.tlsListenPort`, and
`mcpGateway.tlsCertThumbprint` as described on the TLS page.

## Connect A Client

Fetch the onboarding profile for the client type:

```powershell
Invoke-RestMethod http://localhost:7300/api/onboarding/claude-code | ConvertTo-Json -Depth 8
Invoke-RestMethod http://localhost:7300/api/onboarding/codex | ConvertTo-Json -Depth 8
Invoke-RestMethod http://localhost:7300/api/onboarding/generic-mcp | ConvertTo-Json -Depth 8
```

Use the dashboard Onboarding page for copy/paste snippets and client-specific
notes. HTTPS-capable profiles use the HTTPS gateway URL only when the runtime
reports TLS as bound.

## Related Pages

[Configuration](Configuration) |
[TLS and HTTPS](TLS-and-HTTPS) |
[Gateway](Gateway) |
[Onboarding](Onboarding) |
[Windows Firewall and LAN Mode](Windows-Firewall-LAN-Mode) |
[Troubleshooting](Troubleshooting)
