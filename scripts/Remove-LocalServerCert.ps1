# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

<#
.SYNOPSIS
    Roll back the artifacts created by Configure-LocalServerCert.ps1.

.DESCRIPTION
    Removes:
      1. The HTTP.sys sslcert binding on ip:port.
      2. The Windows Firewall rule "MCOS MCP Gateway TLS (...)" for the port.
      3. (Optional, with -DeleteCert) the self-signed cert from
         Cert:\LocalMachine\My matching the chosen subject.
      4. (Optional, with -DeletePublicExport) the exported .cer file.

    Idempotent: missing components are skipped without error.

.PARAMETER CertSubject
    CN field on the cert to delete. Only used when -DeleteCert is passed.
    Default: 'MCOS-LocalServer'.

.PARAMETER TlsListenPort
    Bound port to unbind. Default: 8443.

.PARAMETER ListenIp
    IP literal in the binding. Default: '0.0.0.0'.

.PARAMETER PublicCertExportDir
    Where the public-key .cer file was exported. Only used with
    -DeletePublicExport. Default:
    "$env:PUBLIC\Documents\Master Control Orchestration Server\certs".

.PARAMETER DeleteCert
    Also remove the self-signed cert from LocalMachine\My. Off by default
    (operators often want to keep the cert for a re-bind later).

.PARAMETER DeletePublicExport
    Also delete mcos-server-public.cer from the export dir. Off by default.

.PARAMETER RestartService
    Restart MasterControlProgram afterwards.

.EXAMPLE
    .\Remove-LocalServerCert.ps1 -RestartService

.EXAMPLE
    # Full wipe — binding, firewall, cert, and exported .cer.
    .\Remove-LocalServerCert.ps1 -DeleteCert -DeletePublicExport -RestartService
#>

[CmdletBinding()]
param(
    [Parameter()] [string]$CertSubject = 'MCOS-LocalServer',
    [Parameter()] [uint16]$TlsListenPort = 8443,
    [Parameter()] [string]$ListenIp = '0.0.0.0',
    [Parameter()] [string]$PublicCertExportDir,
    [Parameter()] [switch]$DeleteCert,
    [Parameter()] [switch]$DeletePublicExport,
    [Parameter()] [switch]$RestartService
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($PublicCertExportDir)) {
    $PublicCertExportDir = Join-Path $env:PUBLIC 'Documents\Master Control Orchestration Server\certs'
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal $identity
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Remove-LocalServerCert.ps1 requires an elevated PowerShell."
}

Write-Host "==> Remove-LocalServerCert.ps1"
Write-Host "    TlsListenPort:        $TlsListenPort"
Write-Host "    ListenIp:             $ListenIp"
Write-Host "    DeleteCert:           $DeleteCert"
Write-Host "    DeletePublicExport:   $DeletePublicExport"
Write-Host "    RestartService:       $RestartService"

# -- 1. Remove sslcert binding -------------------------------------------
$ipport = "$($ListenIp):$TlsListenPort"
$existingBinding = (& netsh http show sslcert ipport=$ipport) 2>&1
if ($LASTEXITCODE -eq 0 -and ($existingBinding -join "`n") -match 'Certificate Hash') {
    Write-Host "==> Removing sslcert binding on $ipport"
    & netsh http delete sslcert ipport=$ipport | Out-Null
    Write-Host "    Removed."
} else {
    Write-Host "==> No sslcert binding on $ipport. Skipping."
}

# -- 2. Remove firewall rule ---------------------------------------------
$fwName = "MCOS MCP Gateway TLS ($TlsListenPort)"
$rule = Get-NetFirewallRule -DisplayName $fwName -ErrorAction SilentlyContinue
if ($rule) {
    Write-Host "==> Removing firewall rule '$fwName'"
    Remove-NetFirewallRule -DisplayName $fwName
    Write-Host "    Removed."
} else {
    Write-Host "==> No firewall rule '$fwName'. Skipping."
}

# -- 3. Optionally remove cert -------------------------------------------
if ($DeleteCert) {
    $dn = "CN=$CertSubject"
    $matches = Get-ChildItem Cert:\LocalMachine\My -ErrorAction SilentlyContinue |
        Where-Object { $_.Subject -eq $dn }
    foreach ($c in $matches) {
        Write-Host "==> Removing cert $($c.Thumbprint) ($dn)"
        Remove-Item -Path "Cert:\LocalMachine\My\$($c.Thumbprint)" -Force
    }
    if (-not $matches) {
        Write-Host "==> No cert with subject $dn. Skipping."
    }
}

# -- 4. Optionally remove the exported .cer ------------------------------
if ($DeletePublicExport) {
    $cerPath = Join-Path $PublicCertExportDir 'mcos-server-public.cer'
    if (Test-Path $cerPath) {
        Remove-Item -Path $cerPath -Force
        Write-Host "==> Deleted exported .cer at $cerPath"
    } else {
        Write-Host "==> No exported .cer at $cerPath. Skipping."
    }
}

# -- 5. Restart service ---------------------------------------------------
if ($RestartService) {
    $svc = Get-Service -Name 'MasterControlProgram' -ErrorAction SilentlyContinue
    if ($svc) {
        Write-Host "==> Restarting MasterControlProgram"
        if ($svc.Status -eq 'Running') { Stop-Service -Name 'MasterControlProgram' -Force }
        Start-Service -Name 'MasterControlProgram'
        Write-Host "    Service restarted."
    } else {
        Write-Warning 'MasterControlProgram service is not registered. Skipping restart.'
    }
}

Write-Host ""
Write-Host "==> SUCCESS"
Write-Host ""
Write-Host "    TLS bindings on $ipport have been removed. The gateway will"
Write-Host "    continue serving HTTP on cfg.mcpGateway.listenPort but HTTPS"
Write-Host "    handshakes against this port will now fail until you re-bind"
Write-Host "    a cert."
Write-Host ""
Write-Host "    To finalize the disable, edit mcos.config.json and set:"
Write-Host '      cfg.mcpGateway.tlsEnabled = false'
Write-Host "    so the runtime stops registering the HTTPS URL prefix on"
Write-Host "    subsequent restarts. Then restart MasterControlProgram."
Write-Host ""
Write-Host "    To re-enable TLS later, run:"
Write-Host "      scripts\Configure-LocalServerCert.ps1 -RestartService"
Write-Host "    and set cfg.mcpGateway.tlsEnabled = true (plus the printed"
Write-Host "    thumbprint) in mcos.config.json."
