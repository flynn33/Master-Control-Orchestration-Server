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
    are documented in the wiki:
    https://github.com/flynn33/Master-Control-Orchestration-Server/wiki/TLS-and-HTTPS

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
# Reuse an existing cert with the same subject ONLY when it still covers
# the current set of DNS + IP SANs AND has at least 90 days of validity
# left. Pre-hardening (code review) we reused any same-subject cert
# blindly -- on hosts that gained a new LAN IP via DHCP / VPN / interface
# change since the last run, that cert lacked the new IP SAN and strict
# clients failed validation against `192.168.x.y is not in the SAN list`.
# Re-mint whenever the SAN set drifts.
$dn = "CN=$CertSubject"
$existing = Get-ChildItem Cert:\LocalMachine\My -ErrorAction SilentlyContinue |
    Where-Object { $_.Subject -eq $dn }

function Test-CertCoversRequiredSans {
    param(
        [Parameter(Mandatory=$true)] $Cert,
        [Parameter(Mandatory=$true)] [System.Collections.Generic.List[string]]$RequiredDns,
        [Parameter(Mandatory=$true)] [System.Collections.Generic.List[string]]$RequiredIp
    )
    $sanExt = $Cert.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.17' } | Select-Object -First 1
    if (-not $sanExt) { return [pscustomobject]@{ Covers = $false; Reason = 'cert has no SAN extension'; CertDns = @(); CertIp = @() } }
    # Format($true) returns multi-line text like:
    #   DNS Name=localhost
    #   DNS Name=MYHOST
    #   IP Address=127.0.0.1
    #   IP Address=192.168.1.7
    $text = $sanExt.Format($true)
    $certDns = @()
    $certIp  = @()
    foreach ($line in ($text -split "`r?`n")) {
        $trim = $line.Trim()
        if ($trim -match '^DNS Name=(.+)$') { $certDns += $matches[1].Trim() }
        elseif ($trim -match '^IP Address=(.+)$') { $certIp += $matches[1].Trim() }
    }
    $missingDns = @($RequiredDns | Where-Object { $certDns -notcontains $_ })
    $missingIp  = @($RequiredIp  | Where-Object { $certIp  -notcontains $_ })
    if ($missingDns.Count -gt 0 -or $missingIp.Count -gt 0) {
        $r = "missing SANs: "
        if ($missingDns.Count -gt 0) { $r += "DNS=[$($missingDns -join ',')] " }
        if ($missingIp.Count  -gt 0) { $r += "IP=[$($missingIp -join ',')]" }
        return [pscustomobject]@{ Covers = $false; Reason = $r.Trim(); CertDns = $certDns; CertIp = $certIp }
    }
    return [pscustomobject]@{ Covers = $true; Reason = 'all SANs present'; CertDns = $certDns; CertIp = $certIp }
}

$reuseExisting = $false
$reuseCert = $null
$reuseRejectReason = $null
if ($existing) {
    # Newest first; reuse the freshest cert that still passes validation.
    foreach ($candidate in ($existing | Sort-Object NotAfter -Descending)) {
        $daysLeft = ($candidate.NotAfter - (Get-Date)).TotalDays
        if ($daysLeft -lt 90) {
            $reuseRejectReason = "cert $($candidate.Thumbprint) expires in $([int]$daysLeft) days (< 90-day renewal threshold)"
            continue
        }
        $sanCheck = Test-CertCoversRequiredSans -Cert $candidate -RequiredDns $dnsSans -RequiredIp $ipSans
        if (-not $sanCheck.Covers) {
            $reuseRejectReason = "cert $($candidate.Thumbprint) does not cover the current SANs: $($sanCheck.Reason)"
            continue
        }
        $reuseExisting = $true
        $reuseCert = $candidate
        break
    }
}

if ($reuseExisting) {
    $cert = $reuseCert
    Write-Host "==> Re-using existing cert $($cert.Thumbprint) (subject=$dn, expires $($cert.NotAfter), SANs match current host)"
} else {
    if ($existing -and $reuseRejectReason) {
        Write-Host "==> Existing cert(s) with subject $dn rejected for reuse: $reuseRejectReason"
        Write-Host "    Minting a fresh cert covering the current SAN set."
    }
    $allSans = New-Object System.Collections.Generic.List[string]
    foreach ($d in $dnsSans) { $allSans.Add("DNS=$d") }
    foreach ($i in $ipSans)  { $allSans.Add("IPAddress=$i") }
    # New-SelfSignedCertificate's -DnsName parameter would auto-generate a
    # Subject Alternative Name extension and conflict with the SAN we set
    # via -TextExtension below ("DnsName parameter conflicts with supplied
    # Subject Alternative Name extension"). We need IP SANs (clients
    # connect to literal IPs and most TLS stacks won't match those against
    # DNS-only SANs), so the TextExtension SAN is authoritative -- drop
    # -DnsName entirely.
    $cert = New-SelfSignedCertificate `
        -Subject $dn `
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
