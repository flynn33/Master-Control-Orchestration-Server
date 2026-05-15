# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

<#
.SYNOPSIS
    Provision a self-signed TLS server certificate for the MCOS MCP Gateway
    and bind it to the local HTTP.sys ip:port endpoint via `netsh http add
    sslcert`. Companion to the v0.11.0-alpha.2 TLS dual-bind support in
    cfg.mcpGateway.tlsEnabled / tlsListenPort / tlsCertThumbprint.

.DESCRIPTION
    Runs end-to-end on the host that runs the MCOS service. Designed for
    internal alpha distribution: self-signed per host (each LAN client must
    trust the exported .cer once). Upgrade paths to a local CA or BYO cert
    are noted in `docs\implementation\tls-cert-options.md`.

    Steps:
      1. Generates a self-signed cert in Cert:\LocalMachine\My with SANs
         covering localhost, 127.0.0.1, the machine hostname, and every
         non-loopback IPv4 address on the host. Validity: 5 years.
      2. Exports the public key (.cer, DER) to a known operator-visible
         path so LAN clients can pull it and add it to their trust store.
      3. Removes any prior sslcert binding on the chosen ip:port (idempotent).
      4. Binds the new cert to 0.0.0.0:tlsListenPort via `netsh http add
         sslcert`. HTTP.sys terminates SSL using this binding.
      5. Opens a Windows Firewall inbound rule for TCP/$TlsListenPort with
         the standard MCOS naming convention so Get-NetFirewallRule
         -DisplayName 'MCOS *' lists it alongside the existing rules.
      6. Optionally restarts the MasterControlProgram service so the
         gateway picks up the new TLS bind.
      7. Prints the cert thumbprint + the operator-facing settings to drop
         into mcos.config.json under cfg.mcpGateway.

    All steps are idempotent: running twice produces the same end state.

.PARAMETER CertSubject
    CN field on the generated cert. Default: 'MCOS-LocalServer'.

.PARAMETER TlsListenPort
    TCP port to bind the cert to. Matches cfg.mcpGateway.tlsListenPort.
    Default: 8443.

.PARAMETER ListenIp
    IP literal in the netsh ipport binding. '0.0.0.0' means all interfaces
    (matches the runtime's `https://+:PORT/` URL prefix). Use a specific IP
    to bind only one interface. Default: '0.0.0.0'.

.PARAMETER ValidityYears
    Cert validity. Default: 5.

.PARAMETER PublicCertExportDir
    Directory where the public-key .cer file is exported. Default:
    "$env:PUBLIC\Documents\Master Control Orchestration Server\certs".
    The directory is created if it does not exist. LAN clients copy
    `mcos-server-public.cer` from this directory.

.PARAMETER RestartService
    Restart MasterControlProgram after the cert binding is in place.
    Off by default; pass -RestartService to flip the gateway into TLS
    mode immediately.

.EXAMPLE
    # Most common: generate cert, bind to 0.0.0.0:8443, restart the service.
    .\Configure-LocalServerCert.ps1 -RestartService

.EXAMPLE
    # Custom port + bind only to the LAN-facing IP.
    .\Configure-LocalServerCert.ps1 -TlsListenPort 18443 -ListenIp 192.168.1.7 -RestartService

.NOTES
    Requires elevated PowerShell (Administrator). HTTP.sys + netsh + cert
    store mutation all need admin.

    Self-signed cert means every LAN client must add the exported .cer
    file to its trust store ONCE per machine. See the operator-runbook
    page in the wiki for per-client trust steps.

    Rollback: `.\Remove-LocalServerCert.ps1`.
#>

[CmdletBinding()]
param(
    [Parameter()] [string]$CertSubject = 'MCOS-LocalServer',
    [Parameter()] [uint16]$TlsListenPort = 8443,
    [Parameter()] [string]$ListenIp = '0.0.0.0',
    [Parameter()] [int]$ValidityYears = 5,
    [Parameter()] [string]$PublicCertExportDir,
    [Parameter()] [switch]$RestartService
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Default export dir resolution (path expansion at runtime so $env:PUBLIC
# is correct on the operator's machine).
if ([string]::IsNullOrWhiteSpace($PublicCertExportDir)) {
    $PublicCertExportDir = Join-Path $env:PUBLIC 'Documents\Master Control Orchestration Server\certs'
}

# -- 0. Privilege check ----------------------------------------------------
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal $identity
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Configure-LocalServerCert.ps1 requires an elevated PowerShell. Right-click the shell and run as Administrator."
}

Write-Host "==> Configure-LocalServerCert.ps1"
Write-Host "    CertSubject:         CN=$CertSubject"
Write-Host "    TlsListenPort:       $TlsListenPort"
Write-Host "    ListenIp:            $ListenIp"
Write-Host "    ValidityYears:       $ValidityYears"
Write-Host "    PublicCertExportDir: $PublicCertExportDir"
Write-Host "    RestartService:      $RestartService"

# -- 1. Collect SANs (DNS + IP) -------------------------------------------
$dnsSans = New-Object System.Collections.Generic.List[string]
$dnsSans.Add('localhost')
$hostName = [System.Net.Dns]::GetHostName()
if (-not $dnsSans.Contains($hostName)) { $dnsSans.Add($hostName) }

$ipSans = New-Object System.Collections.Generic.List[string]
$ipSans.Add('127.0.0.1')
try {
    $ipv4 = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction Stop |
        Where-Object { $_.IPAddress -notlike '169.254.*' -and $_.IPAddress -ne '127.0.0.1' } |
        Select-Object -ExpandProperty IPAddress -Unique
    foreach ($ip in $ipv4) {
        if (-not $ipSans.Contains($ip)) { $ipSans.Add($ip) }
    }
} catch {
    Write-Warning "Get-NetIPAddress unavailable; SAN list will only include 127.0.0.1. Reason: $($_.Exception.Message)"
}

Write-Host "    DNS SANs: $($dnsSans -join ', ')"
Write-Host "    IP SANs:  $($ipSans -join ', ')"

# -- 2. Generate self-signed cert in LocalMachine\My ----------------------
# Reuse an existing cert with the same subject if one is already in the
# store, otherwise mint a new one. We match by exact subject so re-running
# the script with the same -CertSubject re-uses the cert (idempotent).
$dn = "CN=$CertSubject"
$existing = Get-ChildItem Cert:\LocalMachine\My -ErrorAction SilentlyContinue |
    Where-Object { $_.Subject -eq $dn }

if ($existing) {
    $cert = $existing | Sort-Object NotAfter -Descending | Select-Object -First 1
    Write-Host "==> Re-using existing cert $($cert.Thumbprint) (subject=$dn, expires $($cert.NotAfter))"
} else {
    $allSans = New-Object System.Collections.Generic.List[string]
    foreach ($d in $dnsSans) { $allSans.Add("DNS=$d") }
    foreach ($i in $ipSans)  { $allSans.Add("IPAddress=$i") }
    # New-SelfSignedCertificate accepts -DnsName for the SAN set but it
    # does NOT honor IPAddress entries via that parameter. Use
    # -Extension manually so both DNS + IP SANs land in the cert.
    # PowerShell 5.1 supports New-SelfSignedCertificate with -DnsName
    # for DNS SANs; IP SANs require the explicit text extension below.
    $cert = New-SelfSignedCertificate `
        -Subject $dn `
        -DnsName $dnsSans `
        -CertStoreLocation 'Cert:\LocalMachine\My' `
        -NotAfter (Get-Date).AddYears($ValidityYears) `
        -KeyUsage DigitalSignature, KeyEncipherment `
        -KeyAlgorithm RSA `
        -KeyLength 2048 `
        -TextExtension @(
            '2.5.29.37={text}1.3.6.1.5.5.7.3.1',
            "2.5.29.17={text}$($allSans -join '&')"
        ) `
        -FriendlyName 'MCOS Local Server Cert'
    Write-Host "==> Generated cert $($cert.Thumbprint) (subject=$dn, expires $($cert.NotAfter))"
}

$thumbprint = $cert.Thumbprint

# -- 3. Export the public key for LAN clients to trust --------------------
if (-not (Test-Path $PublicCertExportDir)) {
    New-Item -ItemType Directory -Path $PublicCertExportDir -Force | Out-Null
}
$cerPath = Join-Path $PublicCertExportDir 'mcos-server-public.cer'
[void](Export-Certificate -Cert $cert -FilePath $cerPath -Type CERT -Force)
Write-Host "==> Exported public key to $cerPath"
Write-Host "    LAN clients must trust this .cer once per machine."

# -- 4. Remove any prior sslcert binding on the chosen ip:port ------------
$ipport = "$($ListenIp):$TlsListenPort"
$existingBinding = (& netsh http show sslcert ipport=$ipport) 2>&1
if ($LASTEXITCODE -eq 0 -and ($existingBinding -join "`n") -match 'Certificate Hash') {
    Write-Host "==> Removing prior sslcert binding on $ipport"
    & netsh http delete sslcert ipport=$ipport | Out-Null
}

# -- 5. Bind the new cert -------------------------------------------------
# AppId is a stable GUID identifying MCOS as the binding owner. Using a
# new GUID per invocation would orphan bindings; we use a fixed one.
$appId = '{F1C3B7E8-1A5D-4E2F-9B6D-7C2A8E0F4D11}'
Write-Host "==> Binding cert $thumbprint to $ipport (appid=$appId)"
$bindOutput = (& netsh http add sslcert ipport=$ipport certhash=$thumbprint appid=$appId certstorename=MY) 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "netsh http add sslcert failed: $($bindOutput -join "`n")"
}
Write-Host "==> Cert binding installed."

# -- 6. Open firewall rule (TCP inbound, profile=Any) ---------------------
$fwName = "MCOS MCP Gateway TLS ($TlsListenPort)"
$existingRule = Get-NetFirewallRule -DisplayName $fwName -ErrorAction SilentlyContinue
if ($existingRule) {
    Write-Host "==> Firewall rule '$fwName' already present."
} else {
    [void](New-NetFirewallRule `
        -DisplayName $fwName `
        -Direction Inbound `
        -Action Allow `
        -Protocol TCP `
        -LocalPort $TlsListenPort `
        -Profile Any `
        -Description 'MCOS MCP Gateway TLS (HTTPS) inbound — v0.11.0-alpha.2 dual-bind.')
    Write-Host "==> Created firewall rule '$fwName'."
}

# -- 7. Restart service (optional) ----------------------------------------
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

# -- 8. Print operator-facing summary -------------------------------------
Write-Host ""
Write-Host "==> SUCCESS"
Write-Host ""
Write-Host "    Certificate thumbprint: $thumbprint"
Write-Host "    Public key:             $cerPath"
Write-Host ""
Write-Host "    Drop these settings into mcos.config.json under cfg.mcpGateway:"
Write-Host '      "tlsEnabled": true,'
Write-Host "      ""tlsListenPort"": $TlsListenPort,"
Write-Host "      ""tlsCertThumbprint"": ""$thumbprint"""
Write-Host ""
Write-Host "    After the service has the new config, the gateway will dual-bind:"
Write-Host "      http://<host>:<listenPort>/mcp   (existing)"
Write-Host "      https://<host>:$TlsListenPort/mcp  (new TLS)"
Write-Host ""
Write-Host "    To roll back: scripts\Remove-LocalServerCert.ps1"

[pscustomobject]@{
    succeeded = $true
    thumbprint = $thumbprint
    ipport = $ipport
    publicCertPath = $cerPath
    tlsListenPort = $TlsListenPort
    configSnippet = @{
        tlsEnabled = $true
        tlsListenPort = $TlsListenPort
        tlsCertThumbprint = $thumbprint
    }
} | ConvertTo-Json -Depth 4
