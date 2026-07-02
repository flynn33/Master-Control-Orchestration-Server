# MCOS Remediation Preflight Report

Date: `2026-05-29`
Commit SHA: `5597a2d91c114bd8893c4f00ba656e036519f390`
Repo path: `D:\Master-Control-Orchestration-Server`

## VERSION.json summary

```json
{
  "current_version": "0.11.0-alpha.2",
  "vcpkg.version-string": "0.11.0-alpha.2"
}
```

## Environment

| Item | Value |
|---|---|
| OS | Microsoft Windows 11 Pro, 10.0.26200, 64-bit |
| PowerShell | 7.5.5 |
| Git | 2.53.0.windows.1 |
| Visual Studio / MSVC | Visual Studio Community 2026 18.4.3; MSVC 19.50.35728 for x64 |
| CMake | 4.2.3-msvc3 at `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`; not on PATH in the default shell |
| CTest | 4.2.3-msvc3 at `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe` |
| vcpkg | `C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg` |
| Windows SDK | 10.0.26100.0 selected by CMake |

## Baseline commands run before source edits

| Command | Result | Notes/log path |
|---|---|---|
| `git switch -c <remediation-branch>` | PASS | Created remediation branch from `main` at `5597a2d91c114bd8893c4f00ba656e036519f390`. |
| `.\scripts\Build-MasterControlOrchestrationServer.ps1 -Preset debug` | PASS | Configure, build, and `ctest --preset debug` completed. 4/4 tests passed. Build emitted 3 existing warnings in `src\MasterControlShell`. Log: `artifacts\mcos-remediation-preflight\baseline-build-debug.log`. |
| `.\scripts\check-mastercontrol-forsetti.ps1` | PASS | `Master Control Forsetti checks passed.` Log: `artifacts\mcos-remediation-preflight\baseline-forsetti-compliance.log`. |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File <package>\scripts\Invoke-MCOSRemediationGates.ps1 -RepoRoot . -LogDirectory artifacts\mcos-remediation-preflight -SkipBuild` | FAIL as expected for audit baseline | `Test-MCOSSecurityDefaults.ps1` failed with 9 unsafe default findings; `Test-MCOSStaticGates.ps1` failed with 14 known-bad literal findings. Logs: `artifacts\mcos-remediation-preflight\Invoke-MCOSRemediationGates-20260529-161006.log`, `Test-MCOSSecurityDefaults.ps1-20260529-161006.log`, `Test-MCOSStaticGates.ps1-20260529-161007.log`. |

## Existing failures before remediation

- Fresh default admin/API bind is `0.0.0.0`.
- Beacon is enabled by default.
- Authentication is globally disabled by default.
- Troubleshooting bypass is enabled by default.
- Open LAN access defaults to true.
- MCP gateway defaults to enabled, `0.0.0.0`, and `lan-trusted`.
- `SecuritySettings.allowOpenLanAccess` model default is true.
- Runtime request handling still contains `AuthenticatedRequestContext context = makeOperatorContext();`.
- Baseline worker still contains unsafe PowerShell `Select-String` command construction.
- Runtime worker launch still appends raw args with `commandLine += L" " + wideFromUtf8(arg);`.
- Bootstrapper still contains `WaitForSingleObject(processInformation.hProcess, INFINITE)`.
- Baseline worker still reports `out["workerVersion"] = "0.9.4";`.
- Baseline worker still contains direct `http://localhost:7300` bridge literals.
- Runtime workflow readiness is still hard-coded with `result.workflowsReadyCount = 0;` and `result.workflowsMissingCount = 1;`.
- `/api/setup/complete` still has the known parse-error-as-empty-body pattern.

## Instruction conflicts recorded

The repository's older `AGENTS.md`, `CLAUDE.md`, and realignment manifest describe a trusted LAN posture with no application-layer authentication. The remediation package supplied for this task requires app-layer authentication and capability checks as hard security gates. This remediation run treats the supplied package as the active scope because it is the explicit current tasking package for `flynn33/Master-Control-Orchestration-Server`.

## Blockers

None for baseline build/test execution. The default shell does not have `cmake` on PATH, but the repository toolchain resolver successfully found Visual Studio CMake, CTest, MSVC, and vcpkg.

## Source edit status

No source files were edited before this preflight report was created.
