---
description: Back up MCOS configuration and runtime state to a timestamped folder on the operator's desktop.
---

Walk the operator through a backup of MCOS state.

This command does NOT use the `mcos_*` HTTP tools — it instructs the operator on PowerShell commands to run, since the backup needs filesystem access at `%ProgramData%\Master Control Orchestration Server\` which isn't exposed via the admin API.

Output this exact PowerShell block for the operator to run:

```powershell
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$root      = "$env:ProgramData\Master Control Orchestration Server"
$dest      = "$env:USERPROFILE\Desktop\mcos-backup-$timestamp"
New-Item -ItemType Directory -Force -Path $dest | Out-Null

# Copy the whole ProgramData tree (config + runtime + governance)
Copy-Item -Recurse -Path "$root\*" -Destination $dest

Compress-Archive -Path $dest -DestinationPath "$dest.zip"
Write-Host "Backup at:  $dest"
Write-Host "Archive:    $dest.zip"
```

After the backup runs, surface what got captured:

```
BACKUP CONTENTS:
- mcos.json — operator config, registered pools
- runtime/events.jsonl — event log history
- runtime/installation-state.json — bootstrapper install state
- governance content — if customized in place
```

To restore on this or another host:

```powershell
Stop-Service MasterControlOrchestrationServer
Copy-Item -Recurse -Force "<backup-dest>\*" "$env:ProgramData\Master Control Orchestration Server\"
Start-Service MasterControlOrchestrationServer
```

Reference: [Maintenance](https://github.com/flynn33/Master-Control-Orchestration-Server/wiki/Maintenance) §Backup.

Optionally also pull the live config via `mcos_config_get` and offer to save it as a JSON file next to the backup (useful for cross-host comparison).
