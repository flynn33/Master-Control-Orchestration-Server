# 13 — MSI Full Round-Trip (Build 48 / v0.4.5-rc.1)

Verdict: **PARTIAL — blocked at msiexec install**. The MSI *builds* cleanly
from current source, carries all 306 payload files, and msiexec accepts and
stages it; but the deferred `RunBootstrapperInstall` custom action fails with
exit 1603, so the round-trip cannot complete via the MSI path. Machine was
restored to a working v0.4.5-rc.1 install through the staged bootstrapper.

## Artifact under test

- Source tree: `G:\Claude\Master-Control-Orchastration-Server\master-control-dashboard-main` at `VERSION.json.current_version = 0.4.5-rc.1`.
- Build command: `scripts\Package-MasterControlOrchestrationServer.ps1 -Preset release -SkipBuild -SkipTests` (build tree from earlier in session, 2915 artifacts, reused).
- Build log: `G:\Claude\mcos_proof_msi_build.log` — `wix 5.0.2+aa65968c` succeeded. 306 files across 95 dirs. No warnings.
- Produced MSI: `G:\Claude\Master-Control-Orchastration-Server\master-control-dashboard-main\dist\packages\release\MasterControlOrchestrationServer-v0.4.5-rc.1-win-x64.msi`
- Size: **23,397,773 bytes (22.31 MB)**; SHA256: **08AD11768EFCAA8D2EAECC626B784B726C40EDD4A55897B914A2AB9139C599C9**
- Copy for testing: `G:\Claude\mcos_test_install_v045.msi`

## Round-trip attempt

| Step | Expected | Result | File |
|---|---|---|---|
| 4. Pre-uninstall probe | bootstrapper install present | install dir present (309 files), no service (zip-ish install), ARP key `MasterControlProgram` = 0.4.5 | `mcos_proof_msi_state_before.txt` |
| 5. Bootstrapper uninstall | clean tear-down | `succeeded:true`, exit 0; service gone, reg key gone. Install dir had to be force-purged after `MasterControlShell.exe` (PIDs 8724 + 15772) released file locks — that's an orthogonal pre-existing bug. | `mcos_proof_msi_uninstall.log` |
| 6. **MSI install** | service registered, dir staged, shortcuts placed | **FAIL exit 1603.** 306 files staged correctly, then deferred CA `RunBootstrapperInstall` returned 1603; rollback removed everything. | `mcos_proof_msi_install.log` |
| 7. Post-MSI health probe | HTTP 200 `/api/health` | skipped — nothing installed | — |
| 8. MSI uninstall | dir + service + reg gone | skipped — rollback already cleaned | — |
| 9. MSI re-install (restore-state) | green | skipped — used staged bootstrapper instead | — |
| 9a. Bootstrapper install (restore) | green | succeeded on 2nd attempt. Service PID 10424, port 7300, validate `valid:true`, `issues:[]`. Post-install `/api/health` = 200 `{"status":"ok"}`, `/api/dashboard` = 200 / 92,257 bytes. | `mcos_proof_msi_reinstall.log`, `mcos_proof_msi_validate_final.json`, `mcos_proof_msi_post_install_health.json` |

## Root cause of the MSI blocker

`installer\Fragments\CustomActions.wxs:29` composes:

```
"[#MasterControlBootstrapperExe]" install "[INSTALLFOLDER]" --skip-shortcuts --skip-uninstall-registration[ServiceFlag][FirewallFlag]
```

`[INSTALLFOLDER]` resolves to `C:\Program Files\Master Control Orchestration Server\` (trailing backslash — WiX convention). In `CustomActionData` this becomes:

```
"C:\Program Files\Master Control Orchestration Server\MasterControlBootstrapper.exe" install "C:\Program Files\Master Control Orchestration Server\" --skip-shortcuts --skip-uninstall-registration
```

`CommandLineToArgvW` treats the `\"` before `--skip-shortcuts` as an escaped quote, so the bootstrapper receives a merged/malformed argument. Log shows:

```
WixQuietExec64:  Error 0x80070001: Command line returned an error.
CustomAction RunBootstrapperInstall returned actual error code 1603
```

(see `mcos_proof_msi_install.log` line 2442-2451)

## Fix path (out of scope for this task)

Either:
1. In `CustomActions.wxs:29` + `:44`, replace `&quot;[INSTALLFOLDER]&quot;` with a stripped-trailing-backslash property — e.g. an immediate `SetProperty Id="InstallDirNoTrail" Value="[INSTALLFOLDER]"` combined with a CA that does `.TrimEnd('\')`, or reformat the arg so the trailing `\` is followed by a space inside the quoted form (`...Server "` → `...Server/ "` works on Win32), or
2. Pass `INSTALLFOLDER` unquoted to the bootstrapper and have the bootstrapper handle trailing-backslash semantics, or
3. Move to `[APPROOT]` / a cleanly-formatted property that WiX doesn't auto-append `\` to.

Also re-worth-noting: the bootstrapper itself has a path-arg-parsing quirk observed in step 5 (un-quoted `Start-Process -ArgumentList @(...)` collapsed `"C:\Program Files\...\Server"` into `"C:\Program"`); wrapping the arg-string in a single quoted form resolved it. That's also a candidate cleanup.

## Files produced this pass

- `G:\Claude\mcos_proof_msi_build.log` (wix build output)
- `G:\Claude\mcos_test_install_v045.msi` (22.31 MB copy)
- `G:\Claude\mcos_proof_msi_state_before.txt`
- `G:\Claude\mcos_proof_msi_uninstall.log` (bootstrapper teardown + force-purge)
- `G:\Claude\mcos_proof_msi_install.log` (verbose msiexec log, contains failure signature)
- `G:\Claude\mcos_proof_msi_install_verify.json` (exit 1603, nothing present)
- `G:\Claude\mcos_proof_msi_reinstall.log` (bootstrapper restore)
- `G:\Claude\mcos_proof_msi_post_install_health.json` (200 / 200 / PID 10424 / port 7300)
- `G:\Claude\mcos_proof_msi_validate_final.json` (`valid:true`, no issues)

Machine left in working state: service running, shortcuts placed, ARP entry registered for `0.4.5`, `/api/health` and `/api/dashboard` both 200.
