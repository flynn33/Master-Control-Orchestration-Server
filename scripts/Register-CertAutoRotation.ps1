# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

<#
.SYNOPSIS
    Registers (or runs) the MCOS TLS certificate auto-rotation job.

.DESCRIPTION
    v0.11.0-alpha.3 — closes the VERSION.json deferred item "Cert
    auto-rotation. ... A renewal-cron PS job is a fit candidate for the
    next pass."

    Configure-LocalServerCert.ps1 already self-renews when it runs: it
    reuses the existing cert only when the SAN set still covers the
    host's current DNS/IP identity AND at least 90 days of validity
    remain, and re-mints + rebinds otherwise. What was missing is
    anything that *runs it on a schedule* — pre-alpha.3 the operator
    had to remember to re-run the script before the 5-year cert (or a
    DHCP-shifted SAN set) went stale.

    This script registers a weekly Windows Scheduled Task (SYSTEM,
    highest privileges) that:
      1. Invokes Configure-LocalServerCert.ps1 from the same scripts
         directory (idempotent reuse-or-renew).
      2. Reads the active sslcert thumbprint for the TLS ip:port from
         netsh and compares it with cfg.mcpGateway.tlsCertThumbprint
         via GET http://127.0.0.1:<AdminPort>/api/config.
      3. On drift (a re-mint happened), patches the thumbprint via
         POST /api/config so discovery and onboarding immediately
         advertise the live cert.

.PARAMETER TaskName
    Scheduled task name. Default: 'MCOS Certificate Auto-Rotation'.

.PARAMETER IntervalDays
    Days between rotation checks. Default: 7. The 90-day renewal
    headroom in Configure-LocalServerCert.ps1 makes weekly checks
    comfortably early.

.PARAMETER At
    Time of day to run the check. Default: 03:30 local.

.PARAMETER TlsListenPort
    Forwarded to Configure-LocalServerCert.ps1. Default: 8443.

.PARAMETER ListenIp
    Forwarded to Configure-LocalServerCert.ps1. Default: 0.0.0.0.

.PARAMETER AdminPort
    MCOS admin listener port for the config thumbprint sync.
    Default: 7300.

.PARAMETER Unregister
    Remove the scheduled task and exit.

.PARAMETER RunRotationNow
    Execute one rotation check immediately (this is what the
    scheduled task invokes). Requires elevation.

.EXAMPLE
    # Register the weekly auto-rotation task (most common).
    .\Register-CertAutoRotation.ps1

.EXAMPLE
    # Custom port + immediate first check.
    .\Register-CertAutoRotation.ps1 -TlsListenPort 18443
    .\Register-CertAutoRotation.ps1 -TlsListenPort 18443 -RunRotationNow

.EXAMPLE
    # Tear the task down.
    .\Register-CertAutoRotation.ps1 -Unregister

.NOTES
    Requires elevated PowerShell (Administrator) — scheduled task
    registration and cert/netsh mutation both need admin.
#>

[CmdletBinding()]
param(
    [Parameter()] [string]$TaskName = 'MCOS Certificate Auto-Rotation',
    [Parameter()] [int]$IntervalDays = 7,
    [Parameter()] [string]$At = '03:30',
    [Parameter()] [uint16]$TlsListenPort = 8443,
    [Parameter()] [string]$ListenIp = '0.0.0.0',
    [Parameter()] [uint16]$AdminPort = 7300,
    [Parameter()] [switch]$Unregister,
    [Parameter()] [switch]$RunRotationNow
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal $identity
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Register-CertAutoRotation.ps1 requires an elevated PowerShell. Right-click the shell and run as Administrator."
}

$scriptsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$configureScript = Join-Path $scriptsDir 'Configure-LocalServerCert.ps1'
$selfPath = $MyInvocation.MyCommand.Path

# -- Unregister mode ---------------------------------------------------------
if ($Unregister) {
    $existing = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    if ($null -eq $existing) {
        Write-Host "==> Scheduled task '$TaskName' is not registered; nothing to do."
        return
    }
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
    Write-Host "==> Removed scheduled task '$TaskName'."
    return
}

# -- RunRotationNow mode (the scheduled task's target) -----------------------
if ($RunRotationNow) {
    if (-not (Test-Path $configureScript)) {
        throw "Configure-LocalServerCert.ps1 not found next to this script at: $configureScript"
    }

    Write-Host "==> Cert rotation check ($(Get-Date -Format o))"

    # 1. Reuse-or-renew. Configure-LocalServerCert.ps1 is idempotent:
    #    it reuses the bound cert when the SAN set still matches and
    #    >90 days of validity remain, and re-mints + rebinds otherwise.
    & $configureScript -TlsListenPort $TlsListenPort -ListenIp $ListenIp

    # 2. Read the thumbprint HTTP.sys is actually serving for ip:port.
    $ipport = "${ListenIp}:${TlsListenPort}"
    $netshOutput = netsh http show sslcert ipport=$ipport 2>$null
    $boundThumbprint = $null
    foreach ($line in $netshOutput) {
        if ($line -match 'Certificate Hash\s*:\s*([0-9a-fA-F]+)') {
            $boundThumbprint = $Matches[1].ToUpperInvariant()
            break
        }
    }
    if ([string]::IsNullOrWhiteSpace($boundThumbprint)) {
        Write-Warning "No sslcert binding found at $ipport after the rotation run; skipping config sync."
        return
    }
    Write-Host "    Bound thumbprint: $boundThumbprint"

    # 3. Sync cfg.mcpGateway.tlsCertThumbprint when it drifted. The
    #    runtime gates TLS advertisement (discovery doc, onboarding
    #    profiles) on this value, so a re-mint without a config sync
    #    would echo the retired thumbprint to operators.
    $configUrl = "http://127.0.0.1:${AdminPort}/api/config"
    try {
        $config = Invoke-RestMethod -Uri $configUrl -Method Get -TimeoutSec 10
    } catch {
        Write-Warning "Could not read $configUrl (service stopped?); skipping config sync. Reason: $($_.Exception.Message)"
        return
    }
    $current = ''
    if ($null -ne $config.mcpGateway -and $null -ne $config.mcpGateway.tlsCertThumbprint) {
        $current = "$($config.mcpGateway.tlsCertThumbprint)".ToUpperInvariant()
    }
    if ($current -eq $boundThumbprint) {
        Write-Host "    Config thumbprint already current; no sync needed."
        return
    }
    $config.mcpGateway.tlsCertThumbprint = $boundThumbprint
    try {
        $response = Invoke-RestMethod -Uri $configUrl -Method Post `
            -ContentType 'application/json' `
            -Headers @{ 'X-Confirm-Unsafe' = '1' } `
            -Body ($config | ConvertTo-Json -Depth 16) -TimeoutSec 15
        Write-Host "    Config thumbprint synced ($current -> $boundThumbprint). Succeeded: $($response.succeeded)"
    } catch {
        Write-Warning "Config thumbprint sync failed: $($_.Exception.Message). Run Configure-LocalServerCert.ps1 manually and merge the printed snippet."
    }
    return
}

# -- Register mode (default) -------------------------------------------------
if (-not (Test-Path $configureScript)) {
    throw "Configure-LocalServerCert.ps1 not found next to this script at: $configureScript. Run from the installed scripts directory."
}

$actionArgs = "-NoProfile -ExecutionPolicy Bypass -File `"$selfPath`" -RunRotationNow" +
    " -TlsListenPort $TlsListenPort -ListenIp $ListenIp -AdminPort $AdminPort"
$action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument $actionArgs
$trigger = New-ScheduledTaskTrigger -Daily -DaysInterval $IntervalDays -At $At
$principalSpec = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -ExecutionTimeLimit (New-TimeSpan -Minutes 15)

$existing = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if ($null -ne $existing) {
    Write-Host "==> Scheduled task '$TaskName' already exists; replacing it with the new definition."
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
}

Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger `
    -Principal $principalSpec -Settings $settings `
    -Description ('MCOS TLS cert auto-rotation: runs Configure-LocalServerCert.ps1 (reuse-or-renew) every ' +
        "$IntervalDays day(s) and syncs cfg.mcpGateway.tlsCertThumbprint on re-mint.") | Out-Null

Write-Host "==> Registered scheduled task '$TaskName' (every $IntervalDays day(s) at $At, SYSTEM, elevated)."
Write-Host "    First run: trigger manually with"
Write-Host "      Start-ScheduledTask -TaskName '$TaskName'"
Write-Host "    or run a one-off check now:"
Write-Host "      .\Register-CertAutoRotation.ps1 -RunRotationNow"
