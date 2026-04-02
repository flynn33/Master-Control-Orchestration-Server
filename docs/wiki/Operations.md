# Master Control Orchestration Server Operations

Build, validate, package, install, and support the current product from the repository-owned tooling.

## Local Build And Validation

```powershell
cmake --preset debug
cmake --build build\debug --config Debug
ctest --test-dir build\debug -C Debug --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-mastercontrol-forsetti.ps1
```

## Staging And Packaging

```powershell
cmake --install build\debug --config Debug --prefix dist\debug
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Package-MasterControlOrchestrationServer.ps1 -Preset release
```

## Preferred Install Entry Points

| Entry point | Role |
| --- | --- |
| `MasterControlOrchestrationServerSetup.exe` | Standard interactive Windows installer entry point |
| `Install-MasterControlOrchestrationServer.ps1` | Diagnostic fallback with desktop logging |
| `MasterControlBootstrapper.exe` | Core lifecycle engine for `preflight`, `install`, `validate`, `upgrade`, `repair`, and `uninstall` |

## Deployment Validation Scripts

| Script | Purpose |
| --- | --- |
| `scripts/Build-MasterControlOrchestrationServer.ps1` | Configure, build, test, and stage local artifacts |
| `scripts/Test-MasterControlOrchestrationServerDeployment.ps1` | End-to-end deployment acceptance harness |
| `scripts/Compare-MasterControlOrchestrationServerDeploymentReports.ps1` | Compare acceptance reports across hosts |
| `scripts/Invoke-MasterControlOrchestrationServerDeploymentMatrix.ps1` | Drive labeled deployment-matrix runs |
| `scripts/Get-MasterControlOrchestrationServerReleaseReadiness.ps1` | Build release-readiness markdown |

## Installed Runtime Surfaces

| Surface | Typical path |
| --- | --- |
| Windows service host | `MasterControlServiceHost.exe` |
| Desktop shell | `MasterControlShell.exe` |
| Browser admin UI | `http://127.0.0.1:7300/` after local install |

## Compatibility Notes

- The public product name is `Master Control Orchestration Server`.
- The legacy Windows service name remains `MasterControlProgram` for upgrade compatibility.
- The legacy uninstall registry key also remains `...\Uninstall\MasterControlProgram` for upgrade compatibility.

## Standard Operator Flow

1. Build and validate locally.
2. Package a release artifact.
3. Install via `MasterControlOrchestrationServerSetup.exe`.
4. Verify service, browser UI, and desktop shell launch behavior.
5. Run deployment acceptance and review readiness artifacts.

See also: [API Reference](API-Reference) | [Automation](Automation) | [Versions](Versions)
