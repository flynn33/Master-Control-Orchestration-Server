# Maintenance

This page covers backup, restore, upgrade, repair, uninstall, certificate
maintenance, and log preservation for MCOS operators.

For daily health checks and routine pool operations, see
[Daily Operations](Daily-Operations).

## Important Paths

| Path | Purpose |
|---|---|
| `C:\Program Files\Master Control Orchestration Server` | Default install directory. |
| `%ProgramData%\MasterControlOrchestrationServer\config\master-control-orchestration-server.json` | Current configuration file. |
| `%ProgramData%\MasterControlOrchestrationServer\config\master-control-program.json` | Legacy fallback configuration file. |
| `%ProgramData%\MasterControlOrchestrationServer\state\` | Runtime state files. |
| `%ProgramData%\MasterControlOrchestrationServer\work\` | Runtime work directory. |
| `%PUBLIC%\Documents\Master Control Orchestration Server\logs\` | Operator-visible persistent logs when emitted by installed components. |

## Back Up

Minimal configuration backup:

```powershell
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$source = "$env:ProgramData\MasterControlOrchestrationServer"
$target = "$env:USERPROFILE\Desktop\mcos-backup-$timestamp"

New-Item -ItemType Directory -Force -Path $target | Out-Null
Copy-Item -Recurse -Force "$source\config" $target
Copy-Item -Recurse -Force "$source\state" $target -ErrorAction SilentlyContinue
```

Archive the backup:

```powershell
Compress-Archive -Path $target -DestinationPath "$target.zip"
```

## Restore

```powershell
Stop-Service MasterControlProgram
Copy-Item -Recurse -Force "$target\*" "$env:ProgramData\MasterControlOrchestrationServer\"
Start-Service MasterControlProgram
```

Then verify:

```powershell
& "C:\Program Files\Master Control Orchestration Server\MasterControlBootstrapper.exe" preflight --json
Invoke-RestMethod http://localhost:7300/api/health | ConvertTo-Json
```

## Upgrade

```powershell
Stop-Service MasterControlProgram
msiexec /i "<path>\MasterControlOrchestrationServer-vA3.11.0-win-x64.msi" /l*v "$env:TEMP\mcos-upgrade.log"
Get-Service MasterControlProgram
```

Configuration and state under ProgramData are preserved by normal MSI upgrades.
If plugin registration was performed as a copy instead of a junction, refresh it
after upgrade:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  "C:\Program Files\Master Control Orchestration Server\Register-McosControlPlugin.ps1"
```

## Repair

```powershell
msiexec /fa "<path-to-current-or-newer-msi>" /l*v "$env:TEMP\mcos-repair.log"
& "C:\Program Files\Master Control Orchestration Server\MasterControlBootstrapper.exe" preflight --json
```

Repair reinstalls MSI payload files and custom-action side effects. It does not
delete operator configuration.

## Uninstall

Preserve configuration and state:

```powershell
$product = Get-WmiObject -Class Win32_Product -Filter "Name = 'Master Control Orchestration Server'"
if ($product) {
  msiexec /uninstall $product.IdentifyingNumber /qn /l*v "$env:TEMP\mcos-uninstall.log"
}
```

A default uninstall preserves ProgramData configuration/state and, when firewall
management was not skipped, already removes the firewall rules (the
`Master Control Orchestration Server - …` rules plus a `MCOS *` sweep). Use the
optional cleanup below only to purge preserved data, or to remove firewall rules
left behind by a `--skip-firewall` install or created manually:

```powershell
Remove-Item -Recurse -Force "$env:ProgramData\MasterControlOrchestrationServer" -ErrorAction SilentlyContinue
Get-NetFirewallRule -DisplayName 'Master Control Orchestration Server - *','MCOS *' -ErrorAction SilentlyContinue | Remove-NetFirewallRule
```

## Reset Configuration

```powershell
Stop-Service MasterControlProgram
Remove-Item "$env:ProgramData\MasterControlOrchestrationServer\config\master-control-orchestration-server.json" -Force
Start-Service MasterControlProgram
```

The runtime regenerates configuration from `buildDefaultConfiguration()`. This
creates a new instance identity and removes persisted pool definitions unless
they are restored separately.

## Certificate Maintenance

Gateway TLS:

```powershell
scripts\Configure-LocalServerCert.ps1 -RestartService
scripts\Register-CertAutoRotation.ps1
scripts\Remove-LocalServerCert.ps1 -RestartService
```

See [TLS and HTTPS](TLS-and-HTTPS) for the distinction between gateway TLS and
admin listener TLS.

## Related Pages

[Operations](Operations) |
[Configuration](Configuration) |
[TLS and HTTPS](TLS-and-HTTPS) |
[Windows Firewall and LAN Mode](Windows-Firewall-LAN-Mode) |
[Troubleshooting](Troubleshooting)
