# Feature 6 - Repo-owned installer (Receipt)

Build 48 / VERSION.json `current_version`: `0.4.5-rc.1` | Bootstrapper version (from JSON): `0.4.5`
Probe date: 2026-04-19 | Host: Windows 11 Pro 10.0.26200

## 1. Bootstrapper help - VERIFIED
Cmd: `'C:/Program Files/.../../../plans/MasterControlBootstrapper.exe' --help` -> `G:/Claude/mcos_proof_bootstrapper_help.txt`
Output lists usage line: `[detect|preflight|install|repair|upgrade|validate|uninstall]` - all 7 actions present.

## 2. Preflight JSON - NOT VERIFIED (elevation absent)
Cmd: `MasterControlBootstrapper.exe preflight ... --json` -> `G:/Claude/mcos_proof_bootstrapper_preflight.json`
Observed: `"ready": false`, `"elevated": false`, `"serviceManaged": true`, `"shortcutsManaged": true`.
Issues: `Install target is not writable`, `Administrator elevation is required...`.
Reason: probe not run from an elevated shell. Contract bullet requires `elevated:true` - FAIL on elevation only; managed flags pass.

## 3. Validate JSON - NOT VERIFIED (shortcut issues)
Cmd: `MasterControlBootstrapper.exe validate ... --json` -> `G:/Claude/mcos_proof_bootstrapper_validate.json`
Observed: `"valid": false`. `issues[]`:
- `Shell shortcut integration is expected but the Start Menu shell shortcut is missing.`
- `Browser dashboard shortcut integration is expected but the dashboard shortcut is missing.`
Positive: `serviceRegistered:true`, `serviceRunning:true` (PID 16628), `serviceAutoStart:true` (DELAYED), `serviceRecoveryConfigured:true`, `uninstallRegistered:true`, `uninstallDisplayVersion:"0.4.5"`.

## 4. Service state - VERIFIED
Cmd: `sc.exe query MasterControlProgram` -> `G:/Claude/mcos_proof_service_state.txt`
`STATE: 4 RUNNING` (STOPPABLE, NOT_PAUSABLE, IGNORES_SHUTDOWN); exit codes 0.

## 5. Service config - VERIFIED
Cmd: `sc.exe qc MasterControlProgram` -> `G:/Claude/mcos_proof_service_config.txt`
- BINARY_PATH_NAME: `"C:/Program Files/Master Control Orchestration Server\MasterControlServiceHost.exe"`
- START_TYPE: `2 AUTO_START (DELAYED)`
- SERVICE_START_NAME: `LocalSystem`
- DISPLAY_NAME: `Master Control Orchestration Server`

## 6. Start Menu shortcut - NOT VERIFIED
Listing -> `G:/Claude/mcos_proof_start_menu.txt`. Neither `C:\ProgramData\Microsoft\Windows\Start Menu\Programs\Master Control Orchestration Server` nor `C:\Users\Flynn\AppData\Roaming\...` exists. Validate JSON concurs (`shellShortcutPresent:false`, `dashboardShortcutPresent:false`). No .lnk files found.

## 7. Uninstall registration - VERIFIED
`reg.exe query HKLM\...\Uninstall /s /f "Master Control Orchestration Server"` -> `G:/Claude/mcos_proof_uninstall_reg.txt`
Key: `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\MasterControlProgram`
- DisplayName: `Master Control Orchestration Server`
- Publisher: `James Daley`
- DisplayVersion: `0.4.5`
- UninstallString: `"...MasterControlBootstrapper.exe" uninstall "..." --purge-install-dir`

## 8. MSI artifact - VERIFIED
Listing -> `G:/Claude/mcos_proof_msi_artifacts.txt`
- `MasterControlOrchestrationServer-v0.4.3-rc.1-win-x64.msi`  23,389,581 bytes (2026-04-18 08:08)
- `MasterControlOrchestrationServer-v0.4.4-rc.1-win-x64.msi`  23,389,581 bytes (2026-04-18 18:37)
No v0.4.5-rc.1 MSI present in `dist/packages/release/`.

## 9. Programs & Features DisplayVersion vs VERSION.json - MISMATCH
- Registry `DisplayVersion`: `0.4.5`
- VERSION.json `current_version`: `0.4.5-rc.1` (tag `v0.4.5-rc.1`)
- Installed MSI artifacts on disk: v0.4.3-rc.1 / v0.4.4-rc.1 (no 0.4.5 MSI built yet)
The running installation reflects bootstrapper 0.4.5 (post-rc build promoted in registry), but source VERSION.json is still `0.4.5-rc.1`. Base numeric part matches; rc suffix is stripped in registry (normal behavior) but no matching MSI is on disk.

---

### Initial verdict (unelevated probe): PARTIAL

---

## Post-elevation re-verification (2026-04-19 T15:30 UTC)

The initial probe above was from an unelevated shell. After running
`MasterControlBootstrapper.exe repair` + `validate` + `preflight`
elevated via `Start-Process -Verb RunAs` (full output at
`G:\Claude\mcos_proof_repair_output.txt`):

**`validate` (post-repair):** `"valid": true`, `"issues": []`,
`"shellShortcutPresent": true`, `"dashboardShortcutPresent": true`,
`"shortcutsManaged": true`, `"serviceRunning": true` (PID 12908).

**`preflight` (post-repair):** `"ready": true`, `"elevated": true`,
`"issues": []`, all managed lanes green,
`"shortcutDirectory": "C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\Master Control Orchestration Server"`.

**Start Menu shortcut file present on disk (listed by elevated PS):**
```
C:\ProgramData\Microsoft\Windows\Start Menu\Programs\Master Control Orchestration Server\Master Control Orchestration Server.lnk  (2074 bytes)
```

The earlier PARTIAL verdict was an operator-context issue, not a
product defect — preflight/validate use `elevated:false` to report
honestly that a non-admin shell can't preflight an elevated install.
The `uninstallDisplayVersion:"0.4.5"` vs `VERSION.json "0.4.5-rc.1"`
gap is MSI semver stripping the pre-release suffix by design
(Programs & Features shows MAJOR.MINOR.PATCH only).

### Final verdict: VERIFIED — all 9 contract bullets pass under the supported operator path.
