# Maintenance

Update, repair, back up, restore, uninstall. The infrequent tasks that nonetheless need to be reproducible.

For day-to-day operations (health check, pool admin, governance approvals) see [Daily Operations](Daily-Operations).

---

## Upgrade to a new version

The MSI handles upgrades cleanly via WiX `MajorUpgrade` — installing a new MSI on a host that already has an older MCOS removes the old version first, then installs the new one. Configuration, pool definitions, and registry hooks survive.

### From a release MSI
```powershell
# 1. Stop the service so the upgrade does not collide with running children
Stop-Service MasterControlOrchestrationServer

# 2. Run the new MSI (interactive)
msiexec /i "<path>\MasterControlOrchestrationServer-vX.Y.Z-win-x64.msi"

# Or silent with full logging
msiexec /i "<...>.msi" /qn /l*v "$env:TEMP\mcos-upgrade-$(Get-Date -Format 'yyyyMMdd-HHmmss').log"

# 3. Confirm it came up
Get-Service MasterControlOrchestrationServer | Format-Table -AutoSize
& "C:\Program Files\Master Control Orchestration Server\MasterControlBootstrapper.exe" preflight --json-output
```

The MSI's `MajorUpgrade` element is configured `Schedule="afterInstallInitialize"` — the old version is removed before the new one's payload is staged, so binaries do not collide.

### What survives an upgrade
- `mcos.json` (operator config)
- `runtime/` (logs, state)
- Operator-edited `governance-profile.json` if you edited it in place
- Registered worker pools (persisted to `mcos.json`)
- Windows Firewall rules (kept across upgrade — they reference the same exe path)

### What gets refreshed
- All binaries under `C:\Program Files\Master Control Orchestration Server\`
- The vendored Forsetti framework files
- `share/MasterControlOrchestrationServer/web/` (browser dashboard assets)
- `share/claude-plugins/mcos-control/` (the Claude Code plugin source)
- `Register-McosControlPlugin.ps1` (the plugin registration helper)
- Start Menu / Desktop shortcuts
- VC++ runtime + Windows App SDK

### Claude Code plugin after upgrade

The dashboard's **Claude Code Control** toggle (v0.6.1+) creates a directory junction by default — upgrades replace the install source in place, so Claude Code picks up the new bridge code on next launch with no operator action.

If you registered the plugin manually with the helper script in copy mode (no `-Symlink`), re-run the helper after each upgrade to refresh the user-side copy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File `
  "C:\Program Files\Master Control Orchestration Server\Register-McosControlPlugin.ps1"
```

Or just toggle the dashboard control off and back on after the upgrade — the off→on cycle drops a fresh junction.

### What needs an explicit re-apply
Nothing automatic — the MSI does not change firewall rules across upgrade, so the `MCOS *` rules from the previous install continue to point at the same `MasterControlServiceHost.exe` path. If you changed install directory between versions, run the firewall snippets again from [Windows Firewall and LAN Mode](Windows-Firewall-LAN-Mode).

---

## Repair a broken installation

If a binary is missing, a registry entry got nuked, or a payload file was corrupted:

```powershell
# Re-run the same MSI as a repair (preserves config + state)
msiexec /fa "<path-to-the-same-or-newer-msi>"

# Confirm
& "C:\Program Files\Master Control Orchestration Server\MasterControlBootstrapper.exe" preflight --json-output
```

`/fa` reinstalls all features unconditionally — file payload, registry, shortcuts, custom-action side effects. `mcos.json` and `runtime/` under `%ProgramData%` are not touched.

If preflight still reports issues after a repair, see [Troubleshooting](Troubleshooting).

---

## Back up configuration and state

The minimal backup is just `mcos.json`. The full backup also includes runtime state, logs, the operator-edited governance profile, and any persisted pools.

```powershell
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$root      = "$env:ProgramData\Master Control Orchestration Server"
$dest      = "$env:USERPROFILE\Desktop\mcos-backup-$timestamp"
New-Item -ItemType Directory -Force -Path $dest | Out-Null

# Copy the whole ProgramData tree (config + runtime + governance)
Copy-Item -Recurse -Path "$root\*" -Destination $dest

Write-Host "Backup at: $dest"
```

If you want a portable archive:

```powershell
Compress-Archive -Path $dest -DestinationPath "$dest.zip"
Write-Host "Archive: $dest.zip"
```

### What's in the backup
| Path | Why it matters |
|---|---|
| `mcos.json` | All operator configuration, including persisted pools |
| `runtime/events.jsonl` | Event log history |
| `runtime/installation-state.json` | Bootstrapper install state |
| Any operator-edited governance content | If you customized `governance-profile.json` in place |

### Restore
Stop the service, copy the backup files back into `%ProgramData%\Master Control Orchestration Server\`, restart the service.

```powershell
Stop-Service MasterControlOrchestrationServer
Copy-Item -Recurse -Force "$dest\*" "$env:ProgramData\Master Control Orchestration Server\"
Start-Service MasterControlOrchestrationServer
```

---

## Add or remove the Start Menu / Desktop shortcuts

The MSI offers both at install time. To change after install, the simplest way is to repair-install with a fresh choice:

```powershell
# This will let you re-walk the McosOptionsDlg dialog
msiexec /fa "<path-to-msi>"
```

Or manually:

```powershell
# Add Desktop shortcut manually
$desktop = [Environment]::GetFolderPath('Desktop')
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut("$desktop\Master Control Orchestration Server.lnk")
$shortcut.TargetPath  = "C:\Program Files\Master Control Orchestration Server\MasterControlOrchestrationServer.exe"
$shortcut.WorkingDirectory = "C:\Program Files\Master Control Orchestration Server"
$shortcut.Save()

# Remove
Remove-Item "$desktop\Master Control Orchestration Server.lnk" -Force
```

The shortcut targets the launcher (`MasterControlOrchestrationServer.exe`), which forwards to the WinUI shell.

---

## Uninstall

### Standard uninstall (preserves config + state)
```powershell
# From an elevated PowerShell
$product = Get-WmiObject -Class Win32_Product -Filter "Name = 'Master Control Orchestration Server'"
if ($product) {
    msiexec /uninstall $product.IdentifyingNumber /qn /l*v "$env:TEMP\mcos-uninstall.log"
}
```

Or launch from **Settings → Apps → Master Control Orchestration Server → Uninstall**.

The MSI removes:
- All binaries under `C:\Program Files\Master Control Orchestration Server\`
- Start Menu entries
- Desktop shortcut (if installed)
- Windows service registration
- Programs and Features entry

The MSI does **not** remove:
- `%ProgramData%\Master Control Orchestration Server\` (config + runtime + logs preserved)
- Windows Firewall rules
- The MCPJungle binary if you placed it separately

### Clean uninstall (removes config + firewall rules)

```powershell
# Standard uninstall first
$product = Get-WmiObject -Class Win32_Product -Filter "Name = 'Master Control Orchestration Server'"
if ($product) { msiexec /uninstall $product.IdentifyingNumber /qn }

# Remove ProgramData directory
Remove-Item -Recurse -Force "$env:ProgramData\Master Control Orchestration Server" -ErrorAction SilentlyContinue

# Remove firewall rules
Get-NetFirewallRule -DisplayName 'MCOS *' -ErrorAction SilentlyContinue | Remove-NetFirewallRule

# Confirm
Get-Service MasterControlOrchestrationServer -ErrorAction SilentlyContinue
```

After a clean uninstall, the host has no MCOS state at all. Re-installing from the MSI gives a fresh `mcos.json` with default values.

---

## Move MCOS to a new host

```mermaid
flowchart LR
    classDef step fill:#031018,stroke:#00F6FF,color:#E6FCFF;
    classDef good fill:#031a14,stroke:#1cf2c1,color:#a8efe0;

    A[1. Backup ProgramData on old host]:::step --> B[2. Standard uninstall on old host]:::step
    B --> C[3. Install MSI on new host]:::step
    C --> D[4. Stop service on new host]:::step
    D --> E[5. Restore ProgramData backup]:::step
    E --> F[6. Apply firewall snippets if needed]:::step
    F --> G[7. Start service]:::step
    G --> H[8. Run preflight]:::good
```

After step 8, AI clients on the LAN need to re-discover the new host. DNS-SD typically picks up the change in seconds; manually-configured clients need their `gatewayMcpUrl` updated to point at the new host's IP.

---

## Reset to factory defaults

```powershell
Stop-Service MasterControlOrchestrationServer

# Delete operator config (keep runtime/ for diagnostic history)
Remove-Item "$env:ProgramData\Master Control Orchestration Server\mcos.json" -Force -ErrorAction SilentlyContinue

# Restart — MCOS regenerates mcos.json with default values from buildDefaultConfiguration
Start-Service MasterControlOrchestrationServer

& "C:\Program Files\Master Control Orchestration Server\MasterControlBootstrapper.exe" preflight --json-output
```

Side effects:
- New `instanceId` (UUID-backed) — DNS-SD instance label changes; LAN clients see a new host
- All registered worker pools are gone (re-add via `POST /api/pools`)
- All security flags reset (`allowOpenLanAccess=false`, etc.)

If the goal is "fix a config that broke" rather than "wipe everything," prefer to fix the specific field in `mcos.json` and restart.

---

## Cross-references

- **Update flow + acceptance harness** → [Operations](Operations)
- **Release gate that protects published MSIs** → [Release Gate](Release-Gate)
- **Configuration field reference** → [Configuration](Configuration)
- **What the MSI installs** → [Packaging and Gateway Binary](Packaging-and-Gateway-Binary)
- **Troubleshooting upgrade / repair failures** → [Troubleshooting](Troubleshooting)
