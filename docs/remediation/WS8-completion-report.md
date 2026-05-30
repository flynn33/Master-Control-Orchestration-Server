# Workstream Completion Report

Workstream ID: `WS8`
Workstream name: `Version, CI, release, and final validation`
Remediation date: `2026-05-29`
Commit SHA before: `5597a2d91c114bd8893c4f00ba656e036519f390`
Commit SHA after: captured in the PR branch history

## Scope completed

- [x] Ensured `VERSION.json` remains the source of truth for runtime, gateway, worker, bootstrapper, shell, EXE resource, package, and MSI metadata.
- [x] Removed stale hard-coded product semver literals from tracked source paths and replaced EXE resource metadata with generated version macros.
- [x] Corrected the bootstrapper build path so `bootstrapperVersion` preserves the full `0.11.0-alpha.2` prerelease value.
- [x] Corrected baseline worker MCP `serverInfo.version` to use `MASTERCONTROL_VERSION`.
- [x] Verified the `windows-2025-vs2026` runner label against the official `actions/runner-images` label table and kept vswhere-based toolchain discovery in CI.
- [x] Tightened release artifact download to use the same verified Windows product-gate run ID, preventing a second API lookup from selecting a different same-SHA run.
- [x] Added repo-local remediation gate scripts so final static validation can be run from the branch itself.
- [x] Ran debug build/CTest, release package validation, MSI generation, static gates, Forsetti compliance, version sync, JS syntax, stale-version grep, and whitespace checks.
- [x] Completed `docs/remediation/FINAL_COMPLETION_REPORT.md`.

## Files changed

| File | Change summary | Why |
|---|---|---|
| `.github/workflows/release.yml` | Added `id: product_gate` and downloads artifacts from `steps.product_gate.outputs.gate_run_id`. | Ensures release publication uses the exact same-SHA product gate run that was verified. |
| `src/MasterControlBaselineToolsWorker/main.cpp` | Worker MCP initialize response now reports `MASTERCONTROL_VERSION`. | Removes stale worker `serverInfo.version` drift. |
| `src/MasterControlBootstrapper/CMakeLists.txt` | Removed base-version compile definition that stripped prerelease suffixes. | Lets bootstrapper JSON/reporting consume generated `MASTERCONTROL_VERSION`. |
| `src/MasterControlServiceHost/MasterControlServiceHost.rc` | Uses `MASTERCONTROL_VERSION_RC` and `MASTERCONTROL_VERSION_RC_STRING`. | Removes source hard-coded EXE version literals. |
| `src/MasterControlBootstrapper/MasterControlBootstrapper.rc` | Uses generated RC version macros. | Keeps bootstrapper/launcher/setup EXE metadata versioned from `VERSION.json`. |
| `src/MasterControlShell/MasterControlShell.rc` | Uses shell generated version macros. | Keeps shell EXE metadata versioned from `VERSION.json`. |
| `src/MasterControlShell/CMakeLists.txt` | Generated shell header now includes RC version macros. | Allows the external MSBuild shell project to compile version resources from generated data. |
| `src/MasterControlShell/MainWindow.xaml.cpp` | Restored CLU fallback toolbar ID as `Operate: Governance`. | Preserves Forsetti bootstrap fallback while retaining WS7 progressive disclosure labels. |
| `scripts/Sync-RepositoryVersionBadges.ps1` | Treats numeric `.rc` VERSIONINFO literals as drift instead of rewriting them. | Enforces generated-header version flow. |
| `scripts/Invoke-MCOSRemediationGates.ps1` | Added repo-local fixed gate orchestrator. | Provides reproducible final static gates from the branch. |
| `scripts/Test-MCOSSecurityDefaults.ps1` | Added repo-local security default static gate. | Keeps package static checks available in-repo. |
| `scripts/Test-MCOSStaticGates.ps1` | Added repo-local known-bad-literal static gate. | Keeps package static checks available in-repo. |
| `docs/remediation/WS8-completion-report.md` | Added this report. | Records WS8 scope and proof. |
| `docs/remediation/FINAL_COMPLETION_REPORT.md` | Added final remediation completion report. | Maps every finding and gate to evidence. |

## Gates run

| Gate ID | Result | Evidence/log path |
|---|---|---|
| `GATE-REL-001` | PASS | `VERSION.json` read by CMake; generated headers used by runtime/shell/worker/bootstrapper/resources; `Sync-RepositoryVersionBadges.ps1 -CheckOnly` passed; tracked stale-version grep returned no matches. |
| `GATE-REL-002` | PASS | `.github/workflows/windows-build-test-package.yml` and `.github/workflows/release.yml` have no `workflow_dispatch`; release downloads from verified `product_gate` run ID. |
| `GATE-REL-003` | PASS | Release package command produced MSI and ZIP under `artifacts/mcos-remediation-WS8-package`; preflight reports `ready: true`. |
| `GATE-FINAL-001` | PASS | Final report at `docs/remediation/FINAL_COMPLETION_REPORT.md`; WS0-WS8 reports present. |

## Tests run

| Command | Result | Notes/log path |
|---|---|---|
| `.\scripts\Build-MasterControlOrchestrationServer.ps1 -Preset debug` | PASS | Debug build completed; CTest 4/4 passed. Existing shell warnings remain in `OverviewSectionControl.xaml.cpp` for C4130 and C4456. |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\Invoke-MCOSRemediationGates.ps1 -RepoRoot D:\Master-Control-Orchestration-Server -LogDirectory D:\Master-Control-Orchestration-Server\artifacts\mcos-remediation-WS8-final -SkipBuild` | PASS | Repo-local security/default static gates passed. |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\check-mastercontrol-forsetti.ps1` | PASS | Forsetti compliance passed after restoring the `dashboard-clu` fallback toolbar ID. |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\Sync-RepositoryVersionBadges.ps1 -CheckOnly` | PASS | README/vcpkg/version resource guard in sync with `VERSION.json`. |
| `node --check resources\web\app.js` | PASS | Browser JavaScript parsed successfully. |
| `git grep -n -E ... stale version/manual dispatch pattern ...` | PASS | Exit code 1 because no tracked matches were found. |
| `git diff --check` | PASS | No whitespace errors; Git reported line-ending normalization warnings only. |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\Package-MasterControlOrchestrationServer.ps1 -Preset release -OutputRoot D:\Master-Control-Orchestration-Server\artifacts\mcos-remediation-WS8-package` | PASS | Release build/test/install/package completed. MSI: `artifacts/mcos-remediation-WS8-package/MasterControlOrchestrationServer-v0.11.0-alpha.2-win-x64.msi` (23,743,096 bytes). ZIP: `artifacts/mcos-remediation-WS8-package/MasterControlOrchestrationServer-v0.11.0-alpha.2-win-x64.zip` (22,614,013 bytes). |
| `powershell.exe ... Test-MCOSSupervisorReachability.ps1 -AdminBaseUrl http://127.0.0.1:7300 -McpBaseUrl http://127.0.0.1:8080` | BLOCKED | Local listener on `0.0.0.0:7300` returns `/api/health` 200 but returns 404 for current supervisor/discovery routes; no MCP listener on 8080. Log: `artifacts/mcos-remediation-WS8-reachability/Test-MCOSSupervisorReachability-20260529-175636.log`. |

## Security impact

WS8 closes version and release-chain integrity gaps. Product version reporting now flows through generated version macros instead of stale literals, release publication cannot download an unverified run artifact after the same-SHA gate check, and final static gates are available in-repo for repeatable remediation validation.

## Remaining risks/blockers

- Live supervisor reachability smoke is blocked by current machine runtime state: a listener owned by process `2804` is bound on `0.0.0.0:7300`, does not expose the new supervisor/discovery routes, and there is no MCP listener on 8080. Code/unit/static validation for endpoint advertisement and diagnostics passed; repeat the live smoke after starting the newly built service host.
- WiX 5.0.2 was installed locally during WS8 so MSI packaging could be validated. The CI workflow already performs the same WiX install step.
- WiX emits WIX1163 warnings for legacy VBScript custom actions in `installer/Fragments/CustomActions.wxs`; MSI generation still succeeds.

## Next workstream readiness

- [x] This workstream is complete.
- [x] Hard gates are passing or blocked only by documented environment limitation.
- [x] No unresolved security regression remains.
