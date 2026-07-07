# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

<#
.SYNOPSIS
    Register a LAN client identity with a local MCOS instance and export its
    configuration bundle.

.DESCRIPTION
    Run on the MCOS host by the local operator. POSTs a LAN client record to
    /api/clients over loopback (loopback callers act with local-operator
    authority, so no app-layer auth is required), then GETs
    /api/clients/{id}/config and writes the bundle to disk. The client is
    registered read-only: NO mutation privileges are granted unless explicit
    -Privilege names are passed. Fails closed on any non-2xx response, malformed
    JSON, or a bundle missing required fields.

    This is an internal-alpha, trusted-LAN aid. It does not mutate the service,
    firewall, URL ACL, or TLS bindings.

.PARAMETER BaseUrl
    Admin base URL. Defaults to the installed instance (installation-state.json)
    or http://127.0.0.1:7300.

.PARAMETER ClientId
    Required. URL-safe client identity slug (the only identity token on the LAN).

.PARAMETER DisplayName
    Optional human-readable name.

.PARAMETER ClientType
    Client type; free-form to allow future types. Defaults to 'codex'.

.PARAMETER HostName
    Optional informational host name.

.PARAMETER NetworkAddress
    Optional informational network address.

.PARAMETER Privilege
    Zero or more privilege names to grant (default: none -> read-only). Known:
    canCreateMcpServers, canModifyMcpServers, canRemoveMcpServers,
    canCreateSubAgents, canModifySubAgents, canRemoveSubAgents,
    canManageClients, canManageModules, canChangeGovernancePolicy.

.PARAMETER AutonomousMode
    Grant autonomous create capability (separate axis from -Privilege). Off by default.

.PARAMETER OutputPath
    Bundle output file. Defaults to .\<ClientId>.json.

.PARAMETER Json
    Emit the JSON report to stdout in addition to writing it.

.PARAMETER Help
    Print help and exit 0.

.EXAMPLE
    pwsh -NoProfile -File scripts/Register-MasterControlLanClient.ps1 `
        -ClientId codex-alpha-01 -ClientType codex -DisplayName "Codex Alpha" `
        -OutputPath .\artifacts\clients\codex-alpha-01.json -Json
#>

[CmdletBinding()]
param(
    [string]$BaseUrl,
    [string]$ClientId,
    [string]$DisplayName,
    [string]$ClientType = 'codex',
    [string]$HostName,
    [string]$NetworkAddress,
    [string[]]$Privilege = @(),
    [switch]$AutonomousMode,
    [string]$OutputPath,
    [string]$InstallDirectory,
    [switch]$WhatIf,
    [switch]$Json,
    [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($Help) {
    Get-Help -Detailed $PSCommandPath
    exit 0
}

# -ClientId is validated here (not as a Mandatory param) so -Help works
# without prompting in a non-interactive session.
if ([string]::IsNullOrWhiteSpace($ClientId)) {
    Write-Error '-ClientId is required.'
    exit 2
}

. (Join-Path $PSScriptRoot 'MasterControlAcceptanceCommon.ps1')

$knownPrivileges = @(
    'canCreateMcpServers', 'canModifyMcpServers', 'canRemoveMcpServers',
    'canCreateSubAgents', 'canModifySubAgents', 'canRemoveSubAgents',
    'canManageClients', 'canManageModules', 'canChangeGovernancePolicy'
)

if (-not $BaseUrl) { $BaseUrl = Resolve-McosAdminBaseUrlFromInstallState -InstallDirectory $InstallDirectory }
if (-not $BaseUrl) { $BaseUrl = 'http://127.0.0.1:7300' }
$BaseUrl = $BaseUrl.TrimEnd('/')

if (-not $OutputPath) { $OutputPath = Join-Path (Get-Location).Path ("$ClientId.json") }

# --- Build the privileges object (all-false unless explicitly granted) -----
$warnings = @()
$privileges = [ordered]@{}
foreach ($name in $knownPrivileges) { $privileges[$name] = $false }
foreach ($granted in $Privilege) {
    if ($knownPrivileges -contains $granted) {
        $privileges[$granted] = $true
    } else {
        $warnings += "Unknown privilege ignored: $granted"
        Write-Warning "Unknown privilege ignored: $granted"
    }
}

$clientRecord = [ordered]@{
    clientId       = $ClientId
    displayName    = if ($DisplayName) { $DisplayName } else { $ClientId }
    clientType     = $ClientType
    hostName       = if ($HostName) { $HostName } else { '' }
    networkAddress = if ($NetworkAddress) { $NetworkAddress } else { '' }
    enabled        = $true
    privileges     = $privileges
    capabilities   = @()
    autonomousMode = [bool]$AutonomousMode
}
$body = $clientRecord | ConvertTo-Json -Depth 6

$registerUrl = "$BaseUrl/api/clients"
$configUrl = "$BaseUrl/api/clients/$ClientId/config"

$report = [ordered]@{
    clientId      = $ClientId
    clientType    = $ClientType
    baseUrl       = $BaseUrl
    registered    = $false
    bundlePath    = $null
    grantedPrivileges = @($Privilege | Where-Object { $knownPrivileges -contains $_ })
    autonomousMode = [bool]$AutonomousMode
    warnings      = $warnings
    error         = $null
    whatIf        = $false
    generatedUtc  = (Get-Date).ToUniversalTime().ToString('o')
}

if ($WhatIf) {
    # Dry-run: report what would be sent without registering.
    $report.whatIf = $true
    $report.requestBody = ($clientRecord)
    $out = $report | ConvertTo-Json -Depth 8
    if ($Json) { $out } else { Write-Host "[WhatIf] Would POST client '$ClientId' to $registerUrl" }
    exit 0
}

# --- Register (fail closed) ------------------------------------------------
$postProbe = Invoke-McosHttpProbe -Method POST -Url $registerUrl -Body $body
if (-not $postProbe.ok) {
    $report.error = "Registration POST failed: status=$($postProbe.statusCode) error=$($postProbe.error) body=$($postProbe.body)"
    $out = $report | ConvertTo-Json -Depth 8
    if ($Json) { $out } else { Write-Error $report.error }
    exit 1
}
$report.registered = $true

# --- Export the config bundle (fail closed) --------------------------------
$configProbe = Invoke-McosHttpProbe -Method GET -Url $configUrl
if (-not $configProbe.ok -or -not $configProbe.jsonValid) {
    $report.error = "Bundle GET failed: status=$($configProbe.statusCode) jsonValid=$($configProbe.jsonValid) error=$($configProbe.error)"
    $out = $report | ConvertTo-Json -Depth 8
    if ($Json) { $out } else { Write-Error $report.error }
    exit 1
}

# Required bundle fields (working-alpha contract): identity header + admin +
# gateway + heartbeat URLs.
$requiredBundleFields = @('clientId', 'mcosServer', 'identification.value', 'gatewayUrl', 'heartbeatUrl')
$missing = Get-McosMissingFields -Json $configProbe.json -RequiredFields $requiredBundleFields
if (@($missing).Count -gt 0) {
    $report.error = "Bundle missing required fields: [$($missing -join ',')]"
    $out = $report | ConvertTo-Json -Depth 8
    if ($Json) { $out } else { Write-Error $report.error }
    exit 1
}

$bundleDir = Split-Path -Parent $OutputPath
if ($bundleDir -and -not (Test-Path -LiteralPath $bundleDir)) {
    New-Item -ItemType Directory -Force -Path $bundleDir | Out-Null
}
Set-Content -LiteralPath $OutputPath -Value $configProbe.body -Encoding UTF8
$report.bundlePath = (Resolve-Path -LiteralPath $OutputPath).Path

$out = $report | ConvertTo-Json -Depth 8
if ($Json) { $out } else { Write-Host "Registered LAN client '$ClientId'. Bundle written to $($report.bundlePath)." }
exit 0
