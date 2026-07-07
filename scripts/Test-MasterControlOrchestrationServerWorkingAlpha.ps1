# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

<#
.SYNOPSIS
    Working-alpha acceptance probe for a locally installed, running MCOS instance.

.DESCRIPTION
    Validates that an installed MCOS service is not merely present but actually
    operating: the admin listener answers every required endpoint with a valid
    schema, the MCP gateway completes initialize/ping/tools-list over JSON-RPC,
    the discovery document advertises routable URLs for the current network
    posture, and (when a bootstrapper is supplied) required HTTP.sys bindings
    are present. A JSON report, a Markdown summary, and per-probe request/
    response evidence are written to the output directory. The script is
    NON-MUTATING: it only performs GET/POST probes and read-only validation.

    This is an internal-alpha, trusted-LAN validation aid. Installing MCOS,
    mutating the service/firewall/URL ACL/TLS bindings, and the final Windows
    build/package are operator-gated actions this script never performs.

.PARAMETER Mode
    Acceptance mode. Only LocalInstalledRuntime is supported today (probe a
    running local install). Reserved for future mutating modes.

.PARAMETER BaseUrl
    Admin base URL (e.g. http://127.0.0.1:7300). Defaults to the installed
    instance's browserUrl/browserPort from installation-state.json when found.

.PARAMETER GatewayUrl
    Optional explicit MCP gateway URL override. Defaults to the gateway URL
    advertised by /api/discovery (wildcard hosts are probed via loopback).

.PARAMETER BootstrapperPath
    Optional path to MasterControlBootstrapper.exe. When supplied, its
    `validate --json` binding posture is incorporated (required-missing
    bindings fail the run).

.PARAMETER OutputDirectory
    Directory for the report and evidence. Defaults to
    .\artifacts\working-alpha-local.

.PARAMETER Json
    Emit the JSON report object to stdout in addition to writing it to disk.

.PARAMETER Help
    Print this help and exit 0.

.EXAMPLE
    pwsh -NoProfile -File scripts/Test-MasterControlOrchestrationServerWorkingAlpha.ps1 `
        -Mode LocalInstalledRuntime -OutputDirectory .\artifacts\working-alpha-local -Json
#>

[CmdletBinding()]
param(
    [ValidateSet('LocalInstalledRuntime')][string]$Mode = 'LocalInstalledRuntime',
    [string]$BaseUrl,
    [string]$GatewayUrl,
    [string]$BootstrapperPath,
    [string]$InstallDirectory,
    [Alias('OutDirectory')]
    [string]$OutputDirectory = (Join-Path (Get-Location).Path 'artifacts\working-alpha-local'),
    [switch]$Certification,
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

# --- Resolve base + output ------------------------------------------------
if (-not $BaseUrl) {
    $BaseUrl = Resolve-McosAdminBaseUrlFromInstallState -InstallDirectory $InstallDirectory
}
if (-not $BaseUrl) {
    $BaseUrl = 'http://127.0.0.1:7300'
    Write-Warning "No -BaseUrl and no install-state found; defaulting to $BaseUrl."
}
$BaseUrl = $BaseUrl.TrimEnd('/')

$evidenceDir = New-McosEvidenceDirectory -Path (Join-Path $OutputDirectory 'evidence')
$checks = New-McosCheckList

# --- Required admin GET routes (from the working-alpha acceptance contract) -
$adminRoutes = @(
    @{ name = 'GET /api/health';                          path = '/api/health';                               fields = @('status', 'time') }
    @{ name = 'GET /api/version';                          path = '/api/version';                              fields = @('version', 'time') }
    @{ name = 'GET /api/health/summary';                   path = '/api/health/summary';                       fields = @('version', 'time', 'gateway', 'pools') }
    @{ name = 'GET /.well-known/mcos.json';                path = '/.well-known/mcos.json';                    fields = @('server', 'gateway', 'admin') }
    @{ name = 'GET /api/discovery';                        path = '/api/discovery';                            fields = @('server', 'gateway', 'admin') }
    @{ name = 'GET /api/gateway/status';                   path = '/api/gateway/status';                       fields = @('state', 'mcpUrl') }
    @{ name = 'GET /api/gateway/health';                   path = '/api/gateway/health';                       fields = @('status', 'mcpUrl') }
    @{ name = 'GET /api/gateway/tools';                    path = '/api/gateway/tools';                        fields = @() }
    @{ name = 'GET /api/onboarding';                       path = '/api/onboarding';                           fields = @('clientTypes') }
    @{ name = 'GET /api/onboarding/codex';                 path = '/api/onboarding/codex?platform=windows';    fields = @('clientType', 'mcp') }
    @{ name = 'GET /api/onboarding/claude-code';           path = '/api/onboarding/claude-code?platform=windows'; fields = @('clientType', 'mcp') }
    @{ name = 'GET /api/governance/profile';               path = '/api/governance/profile';                   fields = @() }
    @{ name = 'GET /api/telemetry/gateway';                path = '/api/telemetry/gateway';                    fields = @() }
    @{ name = 'GET /api/telemetry/clients';                path = '/api/telemetry/clients';                    fields = @() }
    @{ name = 'GET /api/clients';                          path = '/api/clients';                              fields = @() }
    @{ name = 'GET /api/pools';                            path = '/api/pools';                                fields = @() }
    @{ name = 'GET /api/diagnostics/runtime-stats';        path = '/api/diagnostics/runtime-stats';            fields = @('mcpServers', 'subAgents') }
)

$discoveryJson = $null
$summaryJson = $null

foreach ($route in $adminRoutes) {
    $url = "$BaseUrl$($route.path)"
    $probe = Invoke-McosHttpProbe -Method GET -Url $url
    $evidence = Write-McosEvidence -Directory $evidenceDir -Name ("admin_" + $route.name) -Probe $probe

    $missing = @()
    if ($probe.jsonValid) { $missing = Get-McosMissingFields -Json $probe.json -RequiredFields $route.fields }
    $passed = $probe.ok -and $probe.jsonValid -and (@($missing).Count -eq 0)
    $detail = "status=$($probe.statusCode) jsonValid=$($probe.jsonValid)"
    if (@($missing).Count -gt 0) { $detail += " missingFields=[$($missing -join ',')]" }
    if ($probe.error) { $detail += " error=$($probe.error)" }
    Add-McosCheck -Checks $checks -Name $route.name -Passed $passed -Detail $detail -Evidence $evidence | Out-Null

    if ($route.path -eq '/api/discovery' -and $probe.jsonValid) { $discoveryJson = $probe.json }
    if ($route.path -eq '/api/health/summary' -and $probe.jsonValid) { $summaryJson = $probe.json }
}

# --- Posture + routability ------------------------------------------------
# The discovery document reports the current posture. In a trusted-LAN posture
# the advertised admin/gateway URLs MUST be LAN-routable (not loopback/wildcard);
# in a local-only posture loopback advertisement is correct and NOT a failure.
$networkMode = 'local-only'
$advertisedGatewayUrl = $null
if ($discoveryJson) {
    $serverMode = Get-McosJsonPath -Json $discoveryJson -Path 'server.networkMode'
    $posture = Get-McosJsonPath -Json $discoveryJson -Path 'securityPosture'
    if ($serverMode) { $networkMode = "$serverMode" }
    elseif ($posture -and "$posture" -ne 'local-only') { $networkMode = 'trusted-lan' }
    $advertisedGatewayUrl = Get-McosJsonPath -Json $discoveryJson -Path 'gateway.mcpUrl'
}
$requireLan = ($networkMode -eq 'trusted-lan')

if ($discoveryJson -and $advertisedGatewayUrl) {
    $r = Test-McosUrlRoutable -Url "$advertisedGatewayUrl" -RequireLanRoutable:$requireLan
    Add-McosCheck -Checks $checks -Name 'Discovery gateway URL routable for posture' `
        -Passed ([bool]$r.routable) -Detail "networkMode=$networkMode host=$($r.host) $($r.reason)" | Out-Null
} else {
    Add-McosCheck -Checks $checks -Name 'Discovery gateway URL routable for posture' -Passed $false `
        -Detail 'discovery document or gateway.mcpUrl unavailable' | Out-Null
}

# --- MCP gateway JSON-RPC over POST ---------------------------------------
# Probe target: the advertised gateway URL, with wildcard/empty host rewritten
# to loopback for the local probe (we are on the MCOS host).
if (-not $GatewayUrl) {
    if ($advertisedGatewayUrl) {
        $gwHost = Get-McosUrlHost -Url "$advertisedGatewayUrl"
        if ((Test-McosHostWildcard -HostName $gwHost) -or [string]::IsNullOrEmpty($gwHost)) {
            $GatewayUrl = ([string]$advertisedGatewayUrl) -replace [regex]::Escape($gwHost), '127.0.0.1'
        } else {
            $GatewayUrl = "$advertisedGatewayUrl"
        }
    } else {
        $GatewayUrl = 'http://127.0.0.1:8080/mcp'
    }
}

$mcpProbes = @(
    @{ name = 'MCP initialize'; method = 'initialize'; id = 1; params = @{ protocolVersion = '2025-03-26'; capabilities = @{}; clientInfo = @{ name = 'mcos-acceptance-probe'; version = '1.0' } }; fields = @('jsonrpc', 'id', 'result.protocolVersion', 'result.serverInfo.name', 'result.serverInfo.version', 'result.capabilities.tools') }
    @{ name = 'MCP ping';       method = 'ping';       id = 2; params = $null;                                                                                                                        fields = @('jsonrpc', 'id', 'result') }
    @{ name = 'MCP tools/list'; method = 'tools/list'; id = 3; params = $null;                                                                                                                        fields = @('jsonrpc', 'id', 'result.tools') }
)

foreach ($m in $mcpProbes) {
    $probe = Invoke-McosMcpRpc -GatewayUrl $GatewayUrl -RpcMethod $m.method -RpcParams $m.params -Id $m.id
    $evidence = Write-McosEvidence -Directory $evidenceDir -Name ("mcp_" + $m.method) -Probe $probe
    $missing = @()
    if ($probe.jsonValid) { $missing = Get-McosMissingFields -Json $probe.json -RequiredFields $m.fields }
    $passed = $probe.ok -and $probe.jsonValid -and (@($missing).Count -eq 0)
    $detail = "status=$($probe.statusCode) jsonValid=$($probe.jsonValid)"
    if (@($missing).Count -gt 0) { $detail += " missingFields=[$($missing -join ',')]" }
    if ($probe.error) { $detail += " error=$($probe.error)" }
    Add-McosCheck -Checks $checks -Name $m.name -Passed $passed -Detail $detail -Evidence $evidence | Out-Null
}

# --- Runtime working-alpha readiness --------------------------------------
# /api/health/summary carries a `workingAlpha` block. In -Certification mode it
# is a REQUIRED check (readiness must be ready). Without -Certification it is
# informational: an empty client roster / stopped gateway is expected before
# the Gate E second-host run.
$waRequired = [bool]$Certification
$waName = if ($Certification) { 'Runtime workingAlpha readiness' } else { 'Runtime workingAlpha readiness (informational)' }
if ($summaryJson) {
    $wa = Get-McosJsonPath -Json $summaryJson -Path 'workingAlpha'
    if ($wa) {
        $ready = Get-McosJsonPath -Json $wa -Path 'ready'
        $issues = Get-McosJsonPath -Json $wa -Path 'blockingIssues'
        $ids = @()
        if ($issues) { $ids = @($issues | ForEach-Object { $_.id }) }
        Add-McosCheck -Checks $checks -Name $waName `
            -Passed ([bool]$ready) -Detail ("ready=$ready blockingIssues=[$($ids -join ',')]") `
            -Required $waRequired | Out-Null
    } elseif ($Certification) {
        Add-McosCheck -Checks $checks -Name $waName -Passed $false `
            -Detail '/api/health/summary has no workingAlpha readiness block' -Required $true | Out-Null
    }
} elseif ($Certification) {
    Add-McosCheck -Checks $checks -Name $waName -Passed $false `
        -Detail '/api/health/summary unavailable; readiness could not be evaluated' -Required $true | Out-Null
}

# --- Bootstrapper binding posture (optional) ------------------------------
if ($BootstrapperPath -and (Test-Path -LiteralPath $BootstrapperPath) -and $InstallDirectory) {
    try {
        $bindOut = & $BootstrapperPath validate $InstallDirectory --json 2>&1 | Out-String
        Set-Content -LiteralPath (Join-Path $evidenceDir 'bootstrapper-validate.json') -Value $bindOut -Encoding UTF8
        $bindJson = $bindOut | ConvertFrom-Json -ErrorAction Stop
        $bindings = Get-McosJsonPath -Json $bindJson -Path 'bindings'
        $failed = @()
        if ($bindings) {
            foreach ($surface in $bindings.PSObject.Properties.Name) {
                $surfObj = $bindings.$surface
                foreach ($kind in @('firewall', 'urlAcl', 'tls')) {
                    $b = Get-McosJsonPath -Json $surfObj -Path $kind
                    if ($b -and (Get-McosJsonPath -Json $b -Path 'verdict') -eq 'failed') {
                        $failed += "$surface.$kind"
                    }
                }
            }
        }
        Add-McosCheck -Checks $checks -Name 'Required HTTP.sys bindings present' `
            -Passed (@($failed).Count -eq 0) -Detail ("failedBindings=[$($failed -join ',')]") | Out-Null
    } catch {
        Add-McosCheck -Checks $checks -Name 'Required HTTP.sys bindings present' -Passed $false `
            -Detail "bootstrapper validate failed: $($_.Exception.Message)" -Required ([bool]$Certification) | Out-Null
    }
}

# --- Report ---------------------------------------------------------------
$jsonPath = Join-Path $OutputDirectory 'working-alpha-acceptance.json'
$mdPath = Join-Path $OutputDirectory 'working-alpha-acceptance.md'
$context = @{
    mode          = $Mode
    certification = [bool]$Certification
    baseUrl       = $BaseUrl
    gatewayUrl    = $GatewayUrl
    networkMode   = $networkMode
    evidenceDir   = $evidenceDir
    generatedUtc  = (Get-Date).ToUniversalTime().ToString('o')
}
Write-McosReport -Title 'MCOS Working-Alpha Local Runtime Acceptance' -Checks $checks `
    -JsonPath $jsonPath -MarkdownPath $mdPath -Context $context | Out-Null

$passed = Test-McosChecksPassed -Checks $checks

# --- Diagnostics on pass AND fail (if the collector is present) -----------
$diag = Join-Path $PSScriptRoot 'Get-MasterControlOrchestrationServerDeploymentDiagnostics.ps1'
if (Test-Path -LiteralPath $diag) {
    try {
        & $diag -OutputRoot (Join-Path $OutputDirectory 'diagnostics') -Bundle | Out-Null
    } catch {
        Write-Warning "Diagnostics collection failed: $($_.Exception.Message)"
    }
}

if ($Json) {
    Get-Content -LiteralPath $jsonPath -Raw
} else {
    Write-Host "Working-alpha acceptance: $(if ($passed) { 'PASS' } else { 'FAIL' }). Report: $jsonPath"
}
if ($passed) { exit 0 } else { exit 1 }
