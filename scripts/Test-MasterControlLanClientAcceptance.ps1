# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

<#
.SYNOPSIS
    Second-host LAN client acceptance probe for MCOS. Run from a SEPARATE LAN
    machine (not the MCOS host) to prove real client usability.

.DESCRIPTION
    Consumes a client bundle (or an explicit admin/discovery URL + client id)
    and proves, from a second LAN host, that MCOS is usable: it fetches the
    well-known discovery document and /api/discovery, REJECTS advertised URLs
    that are loopback/wildcard (they are unreachable from another host), probes
    gateway status/health, posts a telemetry heartbeat and an authenticated
    client heartbeat (X-MCOS-Client-Id), completes MCP initialize/ping/tools-
    list over JSON-RPC, and confirms the client appears in the telemetry roster
    with a fresh liveness timestamp. Every request/response pair is preserved as
    evidence. The script is non-mutating and exits nonzero on any required
    failure.

    This is an internal-alpha, trusted-LAN validation aid.

.PARAMETER BundlePath
    Path to a client bundle exported by Register-MasterControlLanClient.ps1.
    Provides clientId, admin/gateway/heartbeat URLs.

.PARAMETER AdminBaseUrl
    Admin base URL (alternative to -BundlePath), e.g. http://mcos-host:7300.

.PARAMETER DiscoveryUrl
    Explicit well-known discovery URL (alternative to deriving from admin base).

.PARAMETER GatewayUrl
    Optional MCP gateway URL override.

.PARAMETER ClientId
    Client identity (required when -BundlePath is not supplied).

.PARAMETER ClientType
    Client type. Defaults to 'codex'.

.PARAMETER OutputDirectory
    Directory for the report and evidence. Defaults to .\mcos-lan-acceptance.

.PARAMETER FreshnessSeconds
    Maximum age (seconds) of the telemetry liveness timestamp to count as fresh.
    Defaults to 600.

.PARAMETER Json
    Emit the JSON report to stdout in addition to writing it.

.PARAMETER Help
    Print help and exit 0.

.EXAMPLE
    pwsh -NoProfile -File .\Test-MasterControlLanClientAcceptance.ps1 `
        -BundlePath .\codex-alpha-01.json -OutputDirectory .\mcos-lan-acceptance -Json
#>

[CmdletBinding()]
param(
    [string]$BundlePath,
    [string]$AdminBaseUrl,
    [string]$DiscoveryUrl,
    [string]$GatewayUrl,
    [string]$ClientId,
    [string]$ClientType = 'codex',
    [string]$OutputDirectory = (Join-Path (Get-Location).Path 'mcos-lan-acceptance'),
    [int]$FreshnessSeconds = 600,
    [switch]$Json,
    [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($Help) {
    Get-Help -Detailed $PSCommandPath
    exit 0
}

. (Join-Path $PSScriptRoot 'MasterControlAcceptanceCommon.ps1')

# --- Resolve identity + URLs from the bundle or explicit params -----------
$heartbeatUrl = $null
$telemetryHeartbeatUrl = $null
if ($BundlePath) {
    if (-not (Test-Path -LiteralPath $BundlePath)) {
        Write-Error "Bundle not found: $BundlePath"; exit 2
    }
    $bundle = Get-Content -LiteralPath $BundlePath -Raw | ConvertFrom-Json
    if (-not $ClientId) { $ClientId = "$(Get-McosJsonPath -Json $bundle -Path 'clientId')" }
    if (-not $ClientId) { $ClientId = "$(Get-McosJsonPath -Json $bundle -Path 'identification.value')" }
    if (-not $AdminBaseUrl) { $AdminBaseUrl = "$(Get-McosJsonPath -Json $bundle -Path 'mcosServer')" }
    if (-not $GatewayUrl) { $GatewayUrl = "$(Get-McosJsonPath -Json $bundle -Path 'gatewayUrl')" }
    if (-not $DiscoveryUrl) { $DiscoveryUrl = "$(Get-McosJsonPath -Json $bundle -Path 'discoveryUrl')" }
    $heartbeatUrl = "$(Get-McosJsonPath -Json $bundle -Path 'heartbeatUrl')"
    $telemetryHeartbeatUrl = "$(Get-McosJsonPath -Json $bundle -Path 'telemetryHeartbeatUrl')"
    $bt = Get-McosJsonPath -Json $bundle -Path 'clientType'
    if ($bt) { $ClientType = "$bt" }
}

if (-not $ClientId) { Write-Error '-ClientId is required (or supply -BundlePath).'; exit 2 }
if (-not $AdminBaseUrl -and -not $DiscoveryUrl) {
    Write-Error 'Supply -AdminBaseUrl or -DiscoveryUrl (or a -BundlePath).'; exit 2
}
if ($AdminBaseUrl) { $AdminBaseUrl = $AdminBaseUrl.TrimEnd('/') }
if (-not $DiscoveryUrl -and $AdminBaseUrl) { $DiscoveryUrl = "$AdminBaseUrl/.well-known/mcos.json" }
if (-not $AdminBaseUrl -and $DiscoveryUrl) { $AdminBaseUrl = ($DiscoveryUrl -replace '/\.well-known/mcos\.json.*$', '') }
if (-not $heartbeatUrl) { $heartbeatUrl = "$AdminBaseUrl/api/client/heartbeat" }
if (-not $telemetryHeartbeatUrl) { $telemetryHeartbeatUrl = "$AdminBaseUrl/api/telemetry/heartbeat" }

$evidenceDir = New-McosEvidenceDirectory -Path (Join-Path $OutputDirectory 'evidence')
$checks = New-McosCheckList
$clientHeaders = @{ 'X-MCOS-Client-Id' = $ClientId; 'X-MCOS-Client-Type' = $ClientType }

# --- 1. Well-known discovery (must be reachable from the second host) ------
$wk = Invoke-McosHttpProbe -Method GET -Url $DiscoveryUrl
Write-McosEvidence -Directory $evidenceDir -Name 'well-known' -Probe $wk | Out-Null
Add-McosCheck -Checks $checks -Name 'Well-known discovery reachable' `
    -Passed ($wk.ok -and $wk.jsonValid) -Detail "status=$($wk.statusCode) error=$($wk.error)" | Out-Null

# --- 2. Advertised URLs must be LAN-routable (reject loopback/wildcard) ----
$advGateway = $null
if ($wk.jsonValid) { $advGateway = Get-McosJsonPath -Json $wk.json -Path 'gateway.mcpUrl' }
$advAdmin = $null
if ($wk.jsonValid) { $advAdmin = Get-McosJsonPath -Json $wk.json -Path 'admin.healthUrl' }

# Check the live-advertised gateway URL when available, otherwise the gateway
# URL the client actually uses (from the bundle / -GatewayUrl). Either way a
# loopback/wildcard host is unreachable from this second host and must fail.
$gatewayToCheck = if ($advGateway) { "$advGateway" } else { "$GatewayUrl" }
$gwRoute = Test-McosUrlRoutable -Url $gatewayToCheck -RequireLanRoutable
Add-McosCheck -Checks $checks -Name 'Advertised gateway URL is LAN-routable' `
    -Passed ([bool]$gwRoute.routable) -Detail "url=$gatewayToCheck host=$($gwRoute.host) $($gwRoute.reason)" | Out-Null
if ($advAdmin) {
    $adRoute = Test-McosUrlRoutable -Url ("$advAdmin") -RequireLanRoutable
    Add-McosCheck -Checks $checks -Name 'Advertised admin URL is LAN-routable' `
        -Passed ([bool]$adRoute.routable) -Detail "host=$($adRoute.host) $($adRoute.reason)" | Out-Null
}

# --- 3. /api/discovery ----------------------------------------------------
$disc = Invoke-McosHttpProbe -Method GET -Url "$AdminBaseUrl/api/discovery"
Write-McosEvidence -Directory $evidenceDir -Name 'discovery' -Probe $disc | Out-Null
$discMissing = @()
if ($disc.jsonValid) { $discMissing = Get-McosMissingFields -Json $disc.json -RequiredFields @('server', 'gateway', 'admin') }
Add-McosCheck -Checks $checks -Name 'Discovery document valid' `
    -Passed ($disc.ok -and $disc.jsonValid -and (@($discMissing).Count -eq 0)) `
    -Detail "status=$($disc.statusCode) missing=[$($discMissing -join ',')]" | Out-Null

# --- 4. Gateway status + health -------------------------------------------
foreach ($g in @(
        @{ name = 'Gateway status'; path = '/api/gateway/status'; fields = @('state', 'mcpUrl') },
        @{ name = 'Gateway health'; path = '/api/gateway/health'; fields = @('status', 'mcpUrl') }
    )) {
    $p = Invoke-McosHttpProbe -Method GET -Url "$AdminBaseUrl$($g.path)"
    Write-McosEvidence -Directory $evidenceDir -Name ("admin_" + $g.name) -Probe $p | Out-Null
    $miss = @()
    if ($p.jsonValid) { $miss = Get-McosMissingFields -Json $p.json -RequiredFields $g.fields }
    Add-McosCheck -Checks $checks -Name $g.name -Passed ($p.ok -and $p.jsonValid -and (@($miss).Count -eq 0)) `
        -Detail "status=$($p.statusCode) missing=[$($miss -join ',')]" | Out-Null
}

# --- 5. Telemetry heartbeat -----------------------------------------------
$telBody = @{ clientId = $ClientId; clientType = $ClientType; status = 'online' } | ConvertTo-Json -Compress
$telHb = Invoke-McosHttpProbe -Method POST -Url $telemetryHeartbeatUrl -Body $telBody -Headers $clientHeaders
Write-McosEvidence -Directory $evidenceDir -Name 'telemetry-heartbeat' -Probe $telHb | Out-Null
Add-McosCheck -Checks $checks -Name 'Telemetry heartbeat posted' -Passed $telHb.ok `
    -Detail "status=$($telHb.statusCode) error=$($telHb.error)" | Out-Null

# --- 6. Authenticated client heartbeat (X-MCOS-Client-Id) -----------------
$cliHb = Invoke-McosHttpProbe -Method POST -Url $heartbeatUrl -Body '{}' -Headers $clientHeaders
Write-McosEvidence -Directory $evidenceDir -Name 'client-heartbeat' -Probe $cliHb | Out-Null
Add-McosCheck -Checks $checks -Name 'Authenticated client heartbeat posted' -Passed $cliHb.ok `
    -Detail "status=$($cliHb.statusCode) error=$($cliHb.error)" | Out-Null

# --- 7. MCP initialize / ping / tools-list --------------------------------
if (-not $GatewayUrl) { $GatewayUrl = "$advGateway" }
$mcpProbes = @(
    @{ name = 'MCP initialize'; method = 'initialize'; id = 1; params = @{ protocolVersion = '2025-03-26'; capabilities = @{}; clientInfo = @{ name = 'mcos-lan-acceptance-probe'; version = '1.0' } }; fields = @('jsonrpc', 'id', 'result.protocolVersion', 'result.serverInfo.name', 'result.capabilities.tools') }
    @{ name = 'MCP ping';       method = 'ping';       id = 2; params = $null; fields = @('jsonrpc', 'id', 'result') }
    @{ name = 'MCP tools/list'; method = 'tools/list'; id = 3; params = $null; fields = @('jsonrpc', 'id', 'result.tools') }
)
foreach ($m in $mcpProbes) {
    $p = Invoke-McosMcpRpc -GatewayUrl $GatewayUrl -RpcMethod $m.method -RpcParams $m.params -Id $m.id -Headers $clientHeaders
    Write-McosEvidence -Directory $evidenceDir -Name ("mcp_" + $m.method) -Probe $p | Out-Null
    $miss = @()
    if ($p.jsonValid) { $miss = Get-McosMissingFields -Json $p.json -RequiredFields $m.fields }
    Add-McosCheck -Checks $checks -Name $m.name -Passed ($p.ok -and $p.jsonValid -and (@($miss).Count -eq 0)) `
        -Detail "status=$($p.statusCode) missing=[$($miss -join ',')]" | Out-Null
}

# --- 8. Client liveness in the telemetry roster (fresh timestamp) ---------
$roster = Invoke-McosHttpProbe -Method GET -Url "$AdminBaseUrl/api/telemetry/clients"
Write-McosEvidence -Directory $evidenceDir -Name 'telemetry-clients' -Probe $roster | Out-Null
$liveFresh = $false
$liveDetail = "status=$($roster.statusCode)"
if ($roster.jsonValid -and $roster.json) {
    $entries = @($roster.json)
    $mine = $entries | Where-Object {
        ("$(Get-McosJsonPath -Json $_ -Path 'clientId')" -eq $ClientId) -or
        ("$($_)" -match [regex]::Escape($ClientId))
    }
    foreach ($entry in @($mine)) {
        $ts = Get-McosJsonPath -Json $entry -Path 'lastSeenUtc'
        if (-not $ts) { $ts = Get-McosJsonPath -Json $entry -Path 'lastHeartbeatUtc' }
        if (-not $ts) { $ts = Get-McosJsonPath -Json $entry -Path 'updatedAtUtc' }
        if ($ts) {
            try {
                $age = ((Get-Date).ToUniversalTime() - ([datetimeoffset]"$ts").UtcDateTime).TotalSeconds
                if ($age -le $FreshnessSeconds) { $liveFresh = $true; $liveDetail = "ageSeconds=$([int]$age)" }
            } catch { }
        }
    }
    if (-not $mine) { $liveDetail += ' (client not found in roster)' }
}
Add-McosCheck -Checks $checks -Name 'Client liveness observed with fresh timestamp' -Passed $liveFresh -Detail $liveDetail | Out-Null

# --- Report ---------------------------------------------------------------
$jsonPath = Join-Path $OutputDirectory 'lan-client-acceptance.json'
$mdPath = Join-Path $OutputDirectory 'lan-client-acceptance.md'
$context = @{
    clientId     = $ClientId
    clientType   = $ClientType
    adminBaseUrl = $AdminBaseUrl
    gatewayUrl   = $GatewayUrl
    discoveryUrl = $DiscoveryUrl
    evidenceDir  = $evidenceDir
    generatedUtc = (Get-Date).ToUniversalTime().ToString('o')
}
Write-McosReport -Title 'MCOS Second-Host LAN Client Acceptance' -Checks $checks `
    -JsonPath $jsonPath -MarkdownPath $mdPath -Context $context | Out-Null

$passed = Test-McosChecksPassed -Checks $checks
if ($Json) {
    Get-Content -LiteralPath $jsonPath -Raw
} else {
    Write-Host "LAN client acceptance: $(if ($passed) { 'PASS' } else { 'FAIL' }). Report: $jsonPath"
}
if ($passed) { exit 0 } else { exit 1 }
