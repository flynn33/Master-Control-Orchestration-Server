# Operations

This page collects the existing local commands for building, testing,
packaging, installing, and inspecting MCOS. It does not add new workflows or
checks.

## Build And Test

Debug validation:

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-mastercontrol-forsetti.ps1
```

Release validation:

```powershell
cmake --preset release
cmake --build build\release --config Release
ctest --test-dir build\release -C Release --output-on-failure --timeout 300
```

Toolchain discovery is handled by existing repository scripts such as
`scripts\Resolve-MasterControlToolchain.ps1`.

## Package

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts\Package-MasterControlOrchestrationServer.ps1 `
  -Preset release
```

The package script:

1. Uses `VERSION.json` for the package version.
2. Installs the build output into a staged payload.
3. Runs bootstrapper preflight against the staged payload.
4. Builds the MSI through `installer\Build-Msi.ps1`.
5. Creates a zip artifact.

The MSI is the normal operator-facing installer. Zip-only output requires the
explicit `-AllowZipOnly` option and should be treated as a degraded package.

## Install

```powershell
msiexec /i "dist\packages\release\MasterControlOrchestrationServer-vA3.11.0-win-x64\MasterControlOrchestrationServer-vA3.11.0-win-x64.msi" /l*v "$env:TEMP\mcos-install.log"
```

Service name:

```text
MasterControlProgram
```

Default install directory:

```text
C:\Program Files\Master Control Orchestration Server
```

Default current configuration file:

```text
%ProgramData%\MasterControlOrchestrationServer\config\master-control-orchestration-server.json
```

## Post-Install Checks

```powershell
Get-Service MasterControlProgram

& "C:\Program Files\Master Control Orchestration Server\MasterControlBootstrapper.exe" preflight --json-output

Invoke-RestMethod http://localhost:7300/api/health | ConvertTo-Json
Invoke-RestMethod http://localhost:7300/api/discovery | ConvertTo-Json -Depth 6
Invoke-RestMethod http://localhost:7300/api/gateway/status | ConvertTo-Json -Depth 4
```

## Local Deploy Helper

If present and suitable for the host, use the existing helper:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Deploy-LocalLive.ps1
```

This is a development convenience, not a replacement for MSI packaging.

## Existing Operational Scripts

| Script | Purpose |
|---|---|
| `scripts\Package-MasterControlOrchestrationServer.ps1` | Build package artifacts. |
| `scripts\Get-MasterControlOrchestrationServerReleaseReadiness.ps1` | Produce release-readiness report from existing inputs. |
| `scripts\Test-MasterControlOrchestrationServerDeployment.ps1` | Deployment acceptance harness. |
| `scripts\Compare-MasterControlOrchestrationServerDeploymentReports.ps1` | Compare deployment reports. |
| `scripts\Invoke-MasterControlOrchestrationServerDeploymentMatrix.ps1` | Run deployment matrix scenarios. |
| `scripts\check-mastercontrol-forsetti.ps1` | Forsetti compliance gate. |
| `scripts\Configure-LocalServerCert.ps1` | Provision gateway TLS certificate and binding. |
| `scripts\Remove-LocalServerCert.ps1` | Remove gateway TLS binding and optional cert/export. |
| `scripts\Register-CertAutoRotation.ps1` | Register certificate rotation task. |

## Related Pages

[Quick Start](Quick-Start) |
[Packaging and Gateway Binary](Packaging-and-Gateway-Binary) |
[Release Gate](Release-Gate) |
[Maintenance](Maintenance) |
[Troubleshooting](Troubleshooting)
