# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

<#
.SYNOPSIS
    Single operator-facing working-alpha acceptance orchestrator for MCOS.

.DESCRIPTION
    Drives the running MCOS product through a working-alpha acceptance run and
    emits a reviewable evidence bundle (acceptance-report.json + ACCEPTANCE-
    SUMMARY.md + per-probe evidence, optionally zipped). It composes the shared
    helper module and the existing acceptance scripts; it has no repository or
    GitHub dependency and never mutates host state without an explicit
    authorization switch.

    Modes:
      Inspect            Non-mutating inspection + feature exercise. Never
                         certifies; never mutates host state.
      CertifyLocal       Inspect + local install/service/binding lifecycle
                         gates (each behind its own authorization switch).
      PrepareLanPeerKit  Generate the portable second-host test kit via
                         New-MasterControlHumanAlphaTestKit.ps1.
      CertifyLanPeer     Validate a second-host LAN acceptance report
                         (requires -AuthorizeSecondHostLanTest).
      FullCertification  CertifyLocal + CertifyLanPeer.

    Every mutating gate defaults to NotRunMissingAuthorization (which never
    counts as a pass and prevents DeployableHumanTestableWorkingAlpha
    certification). Any required Fail exits nonzero.

.NOTES
    Requires PowerShell 7+ (uses -SkipHttpErrorCheck via the shared helper).
#>

[CmdletBinding()]
param(
    [ValidateSet('Inspect', 'CertifyLocal', 'PrepareLanPeerKit', 'CertifyLanPeer', 'FullCertification')]
    [string]$Mode = 'Inspect',

    [string]$BaseUrl,
    [string]$GatewayUrl,
    [string]$InstallDirectory,
    [string]$BootstrapperPath,
    [string]$MsiPath,
    [string]$ServiceName = 'MasterControlProgram',
    [string]$OutDirectory,
    [string]$ClientBundlePath,
    [string]$LanPeerReportPath,
    [string]$KitOutputDirectory,

    [switch]$ZipEvidence,
    [switch]$RequireLanRoutable,

    # Operator identity recorded in operator-authorization-record.json when a
    # mutating gate is run.
    [string]$OperatorName,
    [string]$OperatorRole,

    # Explicit mutation authorization switches. Absent => the matching gate is
    # recorded NotRunMissingAuthorization and is never treated as a pass.
    [switch]$AuthorizeFinalBuild,
    [switch]$AuthorizePackage,
    [switch]$AuthorizeInstall,
    [switch]$AuthorizeServiceMutation,
    [switch]$AuthorizeFirewallMutation,
    [switch]$AuthorizeUrlAclMutation,
    [switch]$AuthorizeTlsMutation,
    [switch]$AllowRepairTest,
    [switch]$AuthorizeUninstall,
    [switch]$AllowReinstallTest,
    [switch]$AuthorizeSecondHostLanTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'MasterControlAcceptanceCommon.ps1')

# ---------------------------------------------------------------------------
# Utilities + check accumulation (evidence_bundle_schema.json shape)
# ---------------------------------------------------------------------------

function Get-McosUtcNow { return [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ') }

$script:Checks = [System.Collections.Generic.List[object]]::new()
$script:EvidenceFiles = [System.Collections.Generic.List[string]]::new()
$script:EvidenceDir = $null

function Add-Check {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Id,
        [Parameter(Mandatory)][string]$Title,
        [Parameter(Mandatory)][string]$Category,
        [Parameter(Mandatory)][ValidateSet('Pass', 'Fail', 'Warning', 'SkippedOptional', 'NotRunMissingAuthorization')][string]$Status,
        [bool]$Required = $true,
        [int]$DurationMs = 0,
        [string]$Evidence = '',
        [string]$FailureReason = '',
        [string]$RemediationHint = ''
    )
    $script:Checks.Add([ordered]@{
            id              = $Id
            title           = $Title
            category        = $Category
            required        = $Required
            status          = $Status
            timestampUtc    = (Get-McosUtcNow)
            durationMs      = $DurationMs
            evidence        = $Evidence
            failureReason   = $FailureReason
            remediationHint = $RemediationHint
        }) | Out-Null
}

# Run one HTTP probe, persist its evidence, classify Pass/Fail against the
# accepted status codes + required JSON fields, and record a check.
function Invoke-CheckedProbe {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Id,
        [Parameter(Mandatory)][string]$Title,
        [Parameter(Mandatory)][string]$Category,
        [ValidateSet('GET', 'POST', 'PUT', 'DELETE')][string]$Method = 'GET',
        [Parameter(Mandatory)][string]$Url,
        [string]$Body,
        [hashtable]$Headers,
        [string[]]$RequiredFields,
        [int[]]$AcceptStatus = @(200),
        [bool]$Required = $true,
        [string]$Remediation = ''
    )
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $probe = Invoke-McosHttpProbe -Method $Method -Url $Url -Body $Body -Headers $Headers
    $sw.Stop()
    $ev = ''
    if ($script:EvidenceDir) {
        $ev = Write-McosEvidence -Directory $script:EvidenceDir -Name $Id -Probe $probe
        $script:EvidenceFiles.Add("$ev.meta.json") | Out-Null
    }
    $status = 'Pass'
    $reason = ''
    if ($AcceptStatus -notcontains $probe.statusCode) {
        $status = 'Fail'
        $reason = if ($probe.error) { "Transport error: $($probe.error)" }
        else { "Unexpected status $($probe.statusCode); expected $($AcceptStatus -join ', ')." }
    }
    elseif ($RequiredFields) {
        if (-not $probe.jsonValid) {
            $status = 'Fail'
            $reason = 'Response body was not valid JSON.'
        }
        else {
            $missing = Get-McosMissingFields -Json $probe.json -RequiredFields $RequiredFields
            if ($missing.Count -gt 0) {
                $status = 'Fail'
                $reason = "Missing required fields: $($missing -join ', ')."
            }
        }
    }
    Add-Check -Id $Id -Title $Title -Category $Category -Status $status -Required $Required `
        -DurationMs ([int]$sw.ElapsedMilliseconds) -Evidence $ev -FailureReason $reason -RemediationHint $Remediation
    return $probe
}

# Record a mutating gate that requires explicit authorization. When the switch
# is absent the gate is NotRunMissingAuthorization (never a pass). When present,
# the action script block runs and reports its own checks.
function Invoke-AuthorizedGate {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Id,
        [Parameter(Mandatory)][string]$Title,
        [Parameter(Mandatory)][string]$Category,
        [Parameter(Mandatory)][bool]$Authorized,
        [Parameter(Mandatory)][string]$SwitchName,
        [bool]$Required = $true,
        [scriptblock]$Action
    )
    if (-not $Authorized) {
        Add-Check -Id $Id -Title $Title -Category $Category -Status 'NotRunMissingAuthorization' -Required $Required `
            -FailureReason "Gate not authorized; pass -$SwitchName to run it." `
            -RemediationHint "Re-run with -$SwitchName on a host where this mutation is authorized."
        $script:AnyGateRequested = $true
        return
    }
    $script:AnyGateRun = $true
    if ($Action) { & $Action }
}

# ---------------------------------------------------------------------------
# Resolve target URLs
# ---------------------------------------------------------------------------

if (-not $OutDirectory) {
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $OutDirectory = Join-Path (Get-Location).Path "artifacts/working-alpha-acceptance-$stamp"
}
$script:EvidenceDir = New-McosEvidenceDirectory -Path (Join-Path $OutDirectory 'evidence')

if (-not $BaseUrl) {
    $BaseUrl = Resolve-McosAdminBaseUrlFromInstallState -InstallDirectory $InstallDirectory
    if (-not $BaseUrl) { $BaseUrl = 'http://127.0.0.1:7300' }
}
$BaseUrl = $BaseUrl.TrimEnd('/')

$script:AnyGateRun = $false
$script:AnyGateRequested = $false
$script:BindingsApplied = $false

Write-Host "MCOS working-alpha acceptance | mode=$Mode | base=$BaseUrl"
Write-Host "Evidence: $script:EvidenceDir"

# ===========================================================================
# Non-mutating feature exercise (runs in every mode except pure kit-prep)
# ===========================================================================

function Invoke-AdminApiChecks {
    Invoke-CheckedProbe -Id 'admin-health' -Title 'GET /api/health' -Category 'adminApiProbes' `
        -Url "$BaseUrl/api/health" -RequiredFields @('status') -Remediation 'Start the MCOS service and confirm the admin listener.' | Out-Null
    Invoke-CheckedProbe -Id 'admin-version' -Title 'GET /api/version' -Category 'adminApiProbes' `
        -Url "$BaseUrl/api/version" -RequiredFields @('version') | Out-Null
    Invoke-CheckedProbe -Id 'admin-readiness' -Title 'GET /api/readiness' -Category 'adminApiProbes' `
        -Url "$BaseUrl/api/readiness" | Out-Null
    # 405 + Allow header for an unsupported method on a known route.
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $probe = Invoke-McosHttpProbe -Method DELETE -Url "$BaseUrl/api/health"
    $sw.Stop()
    $ev = Write-McosEvidence -Directory $script:EvidenceDir -Name 'admin-405' -Probe $probe
    $script:EvidenceFiles.Add("$ev.meta.json") | Out-Null
    $ok405 = ($probe.statusCode -eq 405)
    Add-Check -Id 'admin-405' -Title 'Unsupported method returns 405' -Category 'adminApiProbes' `
        -Status ($(if ($ok405) { 'Pass' } else { 'Warning' })) -Required $false -DurationMs ([int]$sw.ElapsedMilliseconds) `
        -Evidence $ev -FailureReason ($(if ($ok405) { '' } else { "Expected 405, got $($probe.statusCode)." }))
    # Structured error on invalid JSON to a POST route.
    $probe2 = Invoke-CheckedProbe -Id 'admin-invalid-json' -Title 'Invalid JSON rejected with structured error' -Category 'adminApiProbes' `
        -Method POST -Url "$BaseUrl/api/config" -Body '{ this is not json ' -AcceptStatus @(400) -Required $false `
        -Remediation 'Malformed JSON should return a 400 with a structured error body.'
}

function Invoke-WorkingAlphaReadinessChecks {
    $probe = Invoke-CheckedProbe -Id 'health-summary' -Title 'GET /api/health/summary (workingAlpha)' -Category 'workingAlphaReadiness' `
        -Url "$BaseUrl/api/health/summary" -RequiredFields @('workingAlpha.ready', 'workingAlpha.blockingIssues') `
        -Remediation 'Readiness fails closed; resolve each blockingIssue on the running host.'
    if ($probe.jsonValid) {
        $ready = Get-McosJsonPath -Json $probe.json -Path 'workingAlpha.ready'
        $issues = Get-McosJsonPath -Json $probe.json -Path 'workingAlpha.blockingIssues'
        $count = if ($issues) { @($issues).Count } else { 0 }
        # Not-ready is not a script failure: it is honest, evidence-backed
        # readiness. It is surfaced as a Warning so the operator sees the count.
        if ($ready -eq $true) {
            Add-Check -Id 'health-summary-ready' -Title 'Working-alpha readiness is ready' -Category 'workingAlphaReadiness' -Status 'Pass' -Required $false
        }
        else {
            Add-Check -Id 'health-summary-ready' -Title 'Working-alpha readiness is ready' -Category 'workingAlphaReadiness' -Status 'Warning' -Required $false `
                -FailureReason "Readiness reports not-ready with $count blocking issue(s)." `
                -RemediationHint 'Resolve the blockingIssues[] entries; see /api/health/summary.'
        }
    }
}

function Invoke-GatewayChecks {
    $statusProbe = Invoke-CheckedProbe -Id 'gateway-status' -Title 'GET /api/gateway/status' -Category 'gatewayJsonRpcProbes' `
        -Url "$BaseUrl/api/gateway/status" -RequiredFields @('state', 'mcpUrl')
    Invoke-CheckedProbe -Id 'gateway-health' -Title 'GET /api/gateway/health' -Category 'gatewayJsonRpcProbes' `
        -Url "$BaseUrl/api/gateway/health" -RequiredFields @('status') | Out-Null
    Invoke-CheckedProbe -Id 'gateway-tools' -Title 'GET /api/gateway/tools' -Category 'gatewayJsonRpcProbes' `
        -Url "$BaseUrl/api/gateway/tools" | Out-Null

    # Resolve the gateway MCP URL: explicit param wins, else advertised mcpUrl
    # rewritten to loopback for the local probe.
    if (-not $GatewayUrl) {
        $advertised = if ($statusProbe.jsonValid) { Get-McosJsonPath -Json $statusProbe.json -Path 'mcpUrl' } else { $null }
        if ($advertised) {
            $gwHost = Get-McosUrlHost -Url "$advertised"
            if ($gwHost -and -not (Test-McosHostLoopback -HostName $gwHost)) {
                $GatewayUrl = ([string]$advertised) -replace [regex]::Escape($gwHost), '127.0.0.1'
            }
            else { $GatewayUrl = "$advertised" }
        }
        else { $GatewayUrl = 'http://127.0.0.1:8080/mcp' }
    }

    $initParams = @{ protocolVersion = '2025-03-26'; capabilities = @{}; clientInfo = @{ name = 'mcos-acceptance-orchestrator'; version = '1.0' } }
    $initProbe = Invoke-McosMcpRpc -GatewayUrl $GatewayUrl -RpcMethod 'initialize' -RpcParams $initParams -Id 1
    $ev = Write-McosEvidence -Directory $script:EvidenceDir -Name 'mcp-initialize' -Probe $initProbe
    $script:EvidenceFiles.Add("$ev.meta.json") | Out-Null
    $initOk = ($initProbe.ok -and $initProbe.jsonValid -and (Get-McosJsonPath -Json $initProbe.json -Path 'result.protocolVersion'))
    Add-Check -Id 'mcp-initialize' -Title 'MCP initialize handshake' -Category 'gatewayJsonRpcProbes' `
        -Status ($(if ($initOk) { 'Pass' } else { 'Fail' })) -Evidence $ev `
        -FailureReason ($(if ($initOk) { '' } else { "initialize did not return a protocolVersion (status $($initProbe.statusCode))." })) `
        -RemediationHint 'Confirm the native MCP gateway is Running and reachable at the resolved URL.'

    $pingProbe = Invoke-McosMcpRpc -GatewayUrl $GatewayUrl -RpcMethod 'ping' -Id 2
    $evp = Write-McosEvidence -Directory $script:EvidenceDir -Name 'mcp-ping' -Probe $pingProbe
    $script:EvidenceFiles.Add("$evp.meta.json") | Out-Null
    Add-Check -Id 'mcp-ping' -Title 'MCP ping' -Category 'gatewayJsonRpcProbes' `
        -Status ($(if ($pingProbe.ok) { 'Pass' } else { 'Fail' })) -Evidence $evp `
        -FailureReason ($(if ($pingProbe.ok) { '' } else { "ping failed (status $($pingProbe.statusCode))." }))

    $listProbe = Invoke-McosMcpRpc -GatewayUrl $GatewayUrl -RpcMethod 'tools/list' -Id 3
    $evl = Write-McosEvidence -Directory $script:EvidenceDir -Name 'mcp-tools-list' -Probe $listProbe
    $script:EvidenceFiles.Add("$evl.meta.json") | Out-Null
    $tools = if ($listProbe.jsonValid) { Get-McosJsonPath -Json $listProbe.json -Path 'result.tools' } else { $null }
    $toolNames = @()
    if ($tools) { $toolNames = @($tools | ForEach-Object { "$($_.name)" }) }
    Add-Check -Id 'mcp-tools-list' -Title 'MCP tools/list returns a catalog' -Category 'gatewayJsonRpcProbes' `
        -Status ($(if ($toolNames.Count -gt 0) { 'Pass' } else { 'Fail' })) -Evidence $evl `
        -FailureReason ($(if ($toolNames.Count -gt 0) { '' } else { 'tools/list returned no tools.' }))

    # Safe tools/call: {pool}__mcos.add over 2 + 3 must return "5" as text.
    $addTool = $toolNames | Where-Object { $_ -match 'mcos\.add$' -or $_ -match '__mcos\.add$' -or $_ -eq 'mcos.add' } | Select-Object -First 1
    if ($addTool) {
        $callParams = @{ name = $addTool; arguments = @{ a = 2; b = 3 } }
        $callProbe = Invoke-McosMcpRpc -GatewayUrl $GatewayUrl -RpcMethod 'tools/call' -RpcParams $callParams -Id 4
        $evc = Write-McosEvidence -Directory $script:EvidenceDir -Name 'mcp-tools-call-add' -Probe $callProbe
        $script:EvidenceFiles.Add("$evc.meta.json") | Out-Null
        $text = if ($callProbe.jsonValid) {
            $content = Get-McosJsonPath -Json $callProbe.json -Path 'result.content'
            if ($content -and @($content).Count -gt 0) { "$(@($content)[0].text)" } else { '' }
        }
        else { '' }
        $callOk = ($callProbe.ok -and $text.Trim() -eq '5')
        Add-Check -Id 'mcp-tools-call-add' -Title "MCP tools/call $addTool (2+3=5)" -Category 'mcpToolCallProbes' `
            -Status ($(if ($callOk) { 'Pass' } else { 'Fail' })) -Evidence $evc `
            -FailureReason ($(if ($callOk) { '' } else { "Expected content[0].text '5', got '$text' (status $($callProbe.statusCode))." })) `
            -RemediationHint 'Confirm the baseline-tools worker pool is ready and serving mcos.add.'
    }
    else {
        Add-Check -Id 'mcp-tools-call-add' -Title 'MCP tools/call safe baseline tool' -Category 'mcpToolCallProbes' `
            -Status 'Fail' -FailureReason 'No mcos.add tool was advertised in tools/list.' `
            -RemediationHint 'Confirm a baseline-tools pool is configured and its worker completed an MCP handshake.'
    }
}

function Invoke-PoolChecks {
    $probe = Invoke-CheckedProbe -Id 'pools-list' -Title 'GET /api/pools' -Category 'poolReadiness' `
        -Url "$BaseUrl/api/pools"
    if (-not $probe.jsonValid) { return }
    $pools = $probe.json
    if ($null -eq $pools) { return }
    $poolArray = @($pools)
    $requiredReady = 0
    $requiredTotal = 0
    foreach ($p in $poolArray) {
        $poolId = if ($p.PSObject.Properties['poolId']) { "$($p.poolId)" } else { '' }
        if (-not $poolId) { continue }
        $minInstances = if ($p.PSObject.Properties['minInstances']) { [int]$p.minInstances } else { 0 }
        if ($minInstances -le 0) { continue }
        $requiredTotal++
        # Handshake-confirmed readiness, not template presence: query saturation.
        $satProbe = Invoke-McosHttpProbe -Method GET -Url "$BaseUrl/api/pools/$([uri]::EscapeDataString($poolId))/saturation"
        $ev = Write-McosEvidence -Directory $script:EvidenceDir -Name "pool-$poolId-saturation" -Probe $satProbe
        $script:EvidenceFiles.Add("$ev.meta.json") | Out-Null
        $readyCount = if ($satProbe.jsonValid) { [int](Get-McosJsonPath -Json $satProbe.json -Path 'readyInstanceCount') } else { 0 }
        $isReady = ($readyCount -gt 0)
        if ($isReady) { $requiredReady++ }
        Add-Check -Id "pool-ready-$poolId" -Title "Required pool '$poolId' has a ready worker instance" -Category 'poolReadiness' `
            -Status ($(if ($isReady) { 'Pass' } else { 'Fail' })) -Evidence $ev `
            -FailureReason ($(if ($isReady) { '' } else { "Pool '$poolId' (minInstances=$minInstances) has no ready instance; readiness must not be template-only." })) `
            -RemediationHint 'A required pool counts ready only after an instance completes a real MCP initialize handshake.'
    }
    if ($requiredTotal -eq 0) {
        Add-Check -Id 'pool-required-present' -Title 'At least one required worker pool is configured' -Category 'poolReadiness' `
            -Status 'Warning' -Required $false -FailureReason 'No pool has minInstances > 0.' `
            -RemediationHint 'Configure a required pool (e.g. baseline-tools) with minInstances >= 1.'
    }
}

function Invoke-LeaseChecks {
    # Safe lease acquire/release exercise on the first required pool, if the
    # route exists. Acquire is a bounded scale-out; release returns it.
    $poolsProbe = Invoke-McosHttpProbe -Method GET -Url "$BaseUrl/api/pools"
    if (-not $poolsProbe.jsonValid) {
        Add-Check -Id 'lease-cycle' -Title 'Lease acquire/release' -Category 'leaseBehavior' -Status 'Warning' -Required $false `
            -FailureReason 'Pools unavailable; lease behavior not exercised.'
        return
    }
    $pool = @($poolsProbe.json) | Where-Object { $_.PSObject.Properties['poolId'] } | Select-Object -First 1
    if (-not $pool) {
        Add-Check -Id 'lease-cycle' -Title 'Lease acquire/release' -Category 'leaseBehavior' -Status 'SkippedOptional' -Required $false `
            -FailureReason 'No pool available to exercise leases.'
        return
    }
    $poolId = "$($pool.poolId)"
    $acqBody = (@{ clientType = 'acceptance-orchestrator'; sessionId = "acc-$([DateTime]::UtcNow.Ticks)" } | ConvertTo-Json -Compress)
    $acqProbe = Invoke-McosHttpProbe -Method POST -Url "$BaseUrl/api/pools/$([uri]::EscapeDataString($poolId))/leases" -Body $acqBody
    $evA = Write-McosEvidence -Directory $script:EvidenceDir -Name "lease-acquire-$poolId" -Probe $acqProbe
    $script:EvidenceFiles.Add("$evA.meta.json") | Out-Null
    $leaseId = if ($acqProbe.jsonValid) { "$(Get-McosJsonPath -Json $acqProbe.json -Path 'leaseId')" } else { '' }
    $acqOk = ($acqProbe.ok -and $leaseId)
    Add-Check -Id 'lease-acquire' -Title "Lease acquire on '$poolId'" -Category 'leaseBehavior' -Required $false `
        -Status ($(if ($acqOk) { 'Pass' } else { 'Warning' })) -Evidence $evA `
        -FailureReason ($(if ($acqOk) { '' } else { "Acquire returned status $($acqProbe.statusCode)." }))
    if ($acqOk) {
        $relProbe = Invoke-McosHttpProbe -Method POST -Url "$BaseUrl/api/leases/$([uri]::EscapeDataString($leaseId))/release"
        $evR = Write-McosEvidence -Directory $script:EvidenceDir -Name "lease-release-$leaseId" -Probe $relProbe
        $script:EvidenceFiles.Add("$evR.meta.json") | Out-Null
        Add-Check -Id 'lease-release' -Title "Lease release on '$poolId'" -Category 'leaseBehavior' -Required $false `
            -Status ($(if ($relProbe.ok) { 'Pass' } else { 'Warning' })) -Evidence $evR `
            -FailureReason ($(if ($relProbe.ok) { '' } else { "Release returned status $($relProbe.statusCode)." }))
    }
}

function Invoke-DiscoveryChecks {
    $discProbe = Invoke-CheckedProbe -Id 'discovery' -Title 'GET /api/discovery' -Category 'discoveryDocuments' `
        -Url "$BaseUrl/api/discovery"
    Invoke-CheckedProbe -Id 'discovery-well-known' -Title 'GET /.well-known/mcos.json' -Category 'discoveryDocuments' `
        -Url "$BaseUrl/.well-known/mcos.json" -Required $false | Out-Null
    if ($discProbe.jsonValid) {
        $advertised = Get-McosJsonPath -Json $discProbe.json -Path 'gateway.mcpUrl'
        $r = Test-McosUrlRoutable -Url "$advertised" -RequireLanRoutable:$RequireLanRoutable
        Add-Check -Id 'discovery-routable' -Title 'Advertised gateway URL is acceptable for posture' -Category 'discoveryDocuments' `
            -Status ($(if ($r.routable) { 'Pass' } else { 'Fail' })) `
            -FailureReason ($(if ($r.routable) { '' } else { $r.reason })) `
            -RemediationHint 'In LAN mode the advertised gateway URL must not be loopback/wildcard.' `
            -Required ([bool]$RequireLanRoutable)
    }
}

function Invoke-OnboardingChecks {
    Invoke-CheckedProbe -Id 'onboarding' -Title 'GET /api/onboarding' -Category 'onboardingProfiles' `
        -Url "$BaseUrl/api/onboarding" | Out-Null
}

function Invoke-ClientChecks {
    # Registration + heartbeat freshness is a mutation of the client roster; do
    # it only when service mutation is authorized so Inspect stays read-only.
    $listProbe = Invoke-CheckedProbe -Id 'clients-list' -Title 'GET /api/clients' -Category 'clientRegistration' `
        -Url "$BaseUrl/api/clients" -Required $false
    if (-not $AuthorizeServiceMutation) {
        Add-Check -Id 'client-heartbeat-freshness' -Title 'Client heartbeat freshness' -Category 'clientHeartbeatFreshness' `
            -Status 'NotRunMissingAuthorization' -Required $false `
            -FailureReason 'Registering a client + posting heartbeats mutates the roster; pass -AuthorizeServiceMutation to exercise.' `
            -RemediationHint 'Run with -AuthorizeServiceMutation to register a probe client and verify fresh liveness.'
        return
    }
    $clientId = "acc-probe-$([DateTime]::UtcNow.Ticks)"
    $regBody = (@{ clientId = $clientId; clientType = 'codex' } | ConvertTo-Json -Compress)
    $regProbe = Invoke-McosHttpProbe -Method POST -Url "$BaseUrl/api/clients" -Body $regBody
    $evReg = Write-McosEvidence -Directory $script:EvidenceDir -Name "client-register-$clientId" -Probe $regProbe
    $script:EvidenceFiles.Add("$evReg.meta.json") | Out-Null
    Add-Check -Id 'client-register' -Title 'POST /api/clients registers a client' -Category 'clientRegistration' `
        -Status ($(if ($regProbe.ok) { 'Pass' } else { 'Fail' })) -Evidence $evReg `
        -FailureReason ($(if ($regProbe.ok) { '' } else { "Registration returned $($regProbe.statusCode)." }))
    $hbBody = (@{ clientId = $clientId } | ConvertTo-Json -Compress)
    $hbProbe = Invoke-McosHttpProbe -Method POST -Url "$BaseUrl/api/telemetry/heartbeat" -Body $hbBody -Headers @{ 'X-MCOS-Client-Id' = $clientId }
    $evHb = Write-McosEvidence -Directory $script:EvidenceDir -Name "client-heartbeat-$clientId" -Probe $hbProbe
    $script:EvidenceFiles.Add("$evHb.meta.json") | Out-Null
    $rosterProbe = Invoke-McosHttpProbe -Method GET -Url "$BaseUrl/api/telemetry/clients"
    $evRoster = Write-McosEvidence -Directory $script:EvidenceDir -Name 'client-roster' -Probe $rosterProbe
    $script:EvidenceFiles.Add("$evRoster.meta.json") | Out-Null
    $fresh = $false
    if ($rosterProbe.jsonValid) {
        $entry = @($rosterProbe.json) | Where-Object { "$($_.clientId)" -eq $clientId } | Select-Object -First 1
        if ($entry) { $fresh = $true }
    }
    Add-Check -Id 'client-heartbeat-freshness' -Title 'Registered client shows fresh liveness' -Category 'clientHeartbeatFreshness' `
        -Status ($(if ($fresh) { 'Pass' } else { 'Fail' })) -Evidence $evRoster `
        -FailureReason ($(if ($fresh) { '' } else { 'Client did not appear with fresh liveness after heartbeat.' })) `
        -RemediationHint 'Liveness must come from heartbeat freshness, not registration alone.'
}

function Invoke-GovernanceChecks {
    Invoke-CheckedProbe -Id 'governance-profile' -Title 'GET /api/governance/profile' -Category 'governanceDecisions' `
        -Url "$BaseUrl/api/governance/profile" | Out-Null
    Invoke-CheckedProbe -Id 'governance-bundles' -Title 'GET /api/governance/bundles' -Category 'governanceDecisions' `
        -Url "$BaseUrl/api/governance/bundles" | Out-Null

    # Approval-required staging (non-mutating): POST /api/governance/profile
    # stages a GovernancePolicyChange (202), which we then REJECT so nothing is
    # applied. This exercises approval-required + rejected without changing the
    # doctrine.
    $changeBody = (@{ doctrine = 'Acceptance probe doctrine (staged, then rejected).' } | ConvertTo-Json -Compress)
    $stageProbe = Invoke-McosHttpProbe -Method POST -Url "$BaseUrl/api/governance/profile" -Body $changeBody
    $evStage = Write-McosEvidence -Directory $script:EvidenceDir -Name 'governance-stage' -Probe $stageProbe
    $script:EvidenceFiles.Add("$evStage.meta.json") | Out-Null
    $deferredId = if ($stageProbe.jsonValid) { "$(Get-McosJsonPath -Json $stageProbe.json -Path 'deferredActionId')" } else { '' }
    $staged = ($stageProbe.statusCode -eq 202 -and $deferredId)
    Add-Check -Id 'governance-approval-required' -Title 'GovernancePolicyChange stages for operator approval (202)' -Category 'governanceDecisions' `
        -Status ($(if ($staged) { 'Pass' } else { 'Fail' })) -Evidence $evStage `
        -FailureReason ($(if ($staged) { '' } else { "Expected 202 + deferredActionId; got $($stageProbe.statusCode). (Requires canChangeGovernancePolicy privilege.)" })) `
        -RemediationHint 'Run over loopback as the local operator, or with a client bearing canChangeGovernancePolicy.'
    if ($staged) {
        $rejBody = (@{ reason = 'acceptance probe: no change intended' } | ConvertTo-Json -Compress)
        $rejProbe = Invoke-McosHttpProbe -Method POST -Url "$BaseUrl/api/clu/approvals/$([uri]::EscapeDataString($deferredId))/reject" -Body $rejBody
        $evRej = Write-McosEvidence -Directory $script:EvidenceDir -Name 'governance-reject' -Probe $rejProbe
        $script:EvidenceFiles.Add("$evRej.meta.json") | Out-Null
        Add-Check -Id 'governance-rejected' -Title 'Rejecting a staged action prevents execution' -Category 'governanceDecisions' `
            -Status ($(if ($rejProbe.ok) { 'Pass' } else { 'Fail' })) -Evidence $evRej `
            -FailureReason ($(if ($rejProbe.ok) { '' } else { "Reject returned $($rejProbe.statusCode)." }))
    }
}

function Invoke-ConfirmGuardChecks {
    # Unsafe config write without the confirm header must be rejected (400 +
    # requiresConfirmation). This is non-mutating (it is rejected).
    $unsafe = (@{ securitySettings = @{ securityProtocolsEnabled = $false } } | ConvertTo-Json -Compress)
    $probe = Invoke-McosHttpProbe -Method POST -Url "$BaseUrl/api/config" -Body $unsafe
    $ev = Write-McosEvidence -Directory $script:EvidenceDir -Name 'confirm-config-unsafe' -Probe $probe
    $script:EvidenceFiles.Add("$ev.meta.json") | Out-Null
    $blocked = ($probe.statusCode -eq 400)
    Add-Check -Id 'confirm-config-unsafe' -Title 'Unsafe config write blocked without X-Confirm-Unsafe' -Category 'confirmGuards' `
        -Status ($(if ($blocked) { 'Pass' } else { 'Fail' })) -Evidence $ev `
        -FailureReason ($(if ($blocked) { '' } else { "Expected 400 requiresConfirmation; got $($probe.statusCode)." })) `
        -RemediationHint 'Security-weakening config writes must require the X-Confirm-Unsafe header.'
    # Destructive diagnostics clear without confirmation must be blocked.
    $clearProbe = Invoke-McosHttpProbe -Method POST -Url "$BaseUrl/api/diagnostics/clear" -Body '{}'
    $evClear = Write-McosEvidence -Directory $script:EvidenceDir -Name 'confirm-diagnostics-clear' -Probe $clearProbe
    $script:EvidenceFiles.Add("$evClear.meta.json") | Out-Null
    $clearBlocked = ($clearProbe.statusCode -eq 400 -or $clearProbe.statusCode -eq 403)
    Add-Check -Id 'confirm-diagnostics-clear' -Title 'Diagnostics clear blocked without confirmation' -Category 'confirmGuards' `
        -Status ($(if ($clearBlocked) { 'Pass' } else { 'Warning' })) -Required $false -Evidence $evClear `
        -FailureReason ($(if ($clearBlocked) { '' } else { "Expected 400/403; got $($clearProbe.statusCode)." }))
}

function Invoke-DiagnosticsChecks {
    Invoke-CheckedProbe -Id 'diagnostics-runtime-stats' -Title 'GET /api/diagnostics/runtime-stats' -Category 'diagnostics' `
        -Url "$BaseUrl/api/diagnostics/runtime-stats" -Required $false | Out-Null
    Invoke-CheckedProbe -Id 'diagnostics-summary' -Title 'GET /api/diagnostics/summary' -Category 'diagnostics' `
        -Url "$BaseUrl/api/diagnostics/summary" | Out-Null
    Invoke-CheckedProbe -Id 'diagnostics-events' -Title 'GET /api/diagnostics/events' -Category 'diagnostics' `
        -Url "$BaseUrl/api/diagnostics/events" -Required $false | Out-Null
}

function Invoke-DashboardChecks {
    $probe = Invoke-McosHttpProbe -Method GET -Url "$BaseUrl/"
    $ev = Write-McosEvidence -Directory $script:EvidenceDir -Name 'dashboard-root' -Probe $probe
    $script:EvidenceFiles.Add("$ev.meta.json") | Out-Null
    $loads = ($probe.statusCode -eq 200 -and $probe.body -match 'Master Control Orchestration Server')
    Add-Check -Id 'dashboard-root' -Title 'Browser dashboard root loads' -Category 'browserDashboard' `
        -Status ($(if ($loads) { 'Pass' } else { 'Fail' })) -Evidence $ev `
        -FailureReason ($(if ($loads) { '' } else { "Dashboard root returned $($probe.statusCode) or missing product marker." }))
    $appProbe = Invoke-McosHttpProbe -Method GET -Url "$BaseUrl/app.js"
    $evApp = Write-McosEvidence -Directory $script:EvidenceDir -Name 'dashboard-appjs' -Probe $appProbe
    $script:EvidenceFiles.Add("$evApp.meta.json") | Out-Null
    $hasReadinessCard = ($appProbe.statusCode -eq 200 -and $appProbe.body -match 'renderWorkingAlphaReadinessCard')
    Add-Check -Id 'dashboard-readiness-card' -Title 'Dashboard serves the working-alpha readiness card' -Category 'browserDashboard' -Required $false `
        -Status ($(if ($hasReadinessCard) { 'Pass' } else { 'Warning' })) -Evidence $evApp `
        -FailureReason ($(if ($hasReadinessCard) { '' } else { 'app.js did not contain the readiness card renderer.' })) `
        -RemediationHint 'Human visual confirmation of the Overview readiness card belongs in ui-human-notes.md.'
}

function Invoke-WinuiChecks {
    $shellExe = $null
    if ($InstallDirectory) {
        $candidate = Join-Path $InstallDirectory 'MasterControlShell.exe'
        if (Test-Path -LiteralPath $candidate) { $shellExe = $candidate }
    }
    if ($shellExe) {
        Add-Check -Id 'winui-shell-present' -Title 'WinUI maintainer shell is installed' -Category 'winuiShell' -Required $false `
            -Status 'Warning' -FailureReason 'Shell present; launch manually and confirm Overview readiness view + honest unavailable state.' `
            -RemediationHint 'Record the manual shell walkthrough in ui-human-notes.md (it cannot be launched headlessly).'
    }
    else {
        Add-Check -Id 'winui-shell-present' -Title 'WinUI maintainer shell' -Category 'winuiShell' -Required $false `
            -Status 'SkippedOptional' -FailureReason 'Shell not installed/packaged on this host (required_if_installed_or_packaged).'
    }
}

# ---------------------------------------------------------------------------
# Non-mutating binding STATE classification (reads only). Actual mutation
# lifecycle gates are added by the CertifyLocal path.
# ---------------------------------------------------------------------------

function Invoke-BindingStateChecks {
    $installStatePath = $null
    foreach ($c in @(
            $(if ($InstallDirectory) { Join-Path $InstallDirectory 'installation-state.json' } else { $null }),
            (Join-Path ${env:ProgramFiles} 'Master Control Orchestration Server\installation-state.json'))) {
        if ($c -and (Test-Path -LiteralPath $c)) { $installStatePath = $c; break }
    }
    if ($installStatePath) {
        Add-Check -Id 'install-state' -Title 'Installation state file present' -Category 'installState' -Required $false `
            -Status 'Pass' -Evidence $installStatePath
    }
    else {
        Add-Check -Id 'install-state' -Title 'Installation state file present' -Category 'installState' -Required $false `
            -Status 'SkippedOptional' -FailureReason 'No installation-state.json found; running against a non-installed instance.'
    }

    $svc = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
    if ($svc) {
        Add-Check -Id 'service-state' -Title 'Windows service state' -Category 'serviceState' -Required $false `
            -Status ($(if ($svc.Status -eq 'Running') { 'Pass' } else { 'Warning' })) `
            -FailureReason ($(if ($svc.Status -eq 'Running') { '' } else { "Service state is $($svc.Status)." }))
    }
    else {
        Add-Check -Id 'service-state' -Title 'Windows service state' -Category 'serviceState' -Required $false `
            -Status 'SkippedOptional' -FailureReason 'MCOS service is not registered on this host.'
    }

    # Listener state: is the admin port answering (already proven by adminApi)?
    Add-Check -Id 'process-listener-state' -Title 'Admin listener answering' -Category 'processListenerState' -Required $false `
        -Status ($(if (($script:Checks | Where-Object { $_.id -eq 'admin-health' -and $_.status -eq 'Pass' })) { 'Pass' } else { 'Warning' })) `
        -FailureReason 'Derived from GET /api/health reachability.'

    # URL ACL / firewall / TLS state via the bootstrapper validate --json (a
    # non-mutating read) when a bootstrapper path is supplied.
    if ($BootstrapperPath -and (Test-Path -LiteralPath $BootstrapperPath)) {
        try {
            $raw = & $BootstrapperPath validate --json 2>$null
            $bootJson = ($raw | Out-String) | ConvertFrom-Json -ErrorAction Stop
            Set-Content -LiteralPath (Join-Path $script:EvidenceDir 'bootstrapper-validate.json') -Value ($bootJson | ConvertTo-Json -Depth 12) -Encoding UTF8
            $script:EvidenceFiles.Add('bootstrapper-validate.json') | Out-Null
            # Classify from the real validate --json fields (valid / issues /
            # per-rule firewall booleans / service*). There is no discrete
            # urlAcl or tls node, so URL ACL is derived from overall binding
            # validity and TLS is reported optional-and-unsurfaced honestly.
            $bootValid = [bool](Get-McosJsonPath -Json $bootJson -Path 'valid')
            $issues = Get-McosJsonPath -Json $bootJson -Path 'issues'
            $issueText = if ($issues) { (@($issues) -join '; ') } else { '' }
            $gwFw = [bool](Get-McosJsonPath -Json $bootJson -Path 'gatewayFirewallRulePresent')
            Add-Check -Id 'firewall-state' -Title 'Firewall: inbound MCP gateway rule present' -Category 'firewallState' -Required $false `
                -Status ($(if ($gwFw) { 'Pass' } else { 'Warning' })) `
                -FailureReason ($(if ($gwFw) { '' } else { 'Inbound MCP gateway firewall rule is not present.' }))
            Add-Check -Id 'urlacl-state' -Title 'HTTP.sys bindings valid (URL ACL surface)' -Category 'urlAclState' -Required $false `
                -Status ($(if ($bootValid) { 'Pass' } else { 'Warning' })) `
                -FailureReason ($(if ($bootValid) { '' } else { "Bootstrapper reports binding issues: $issueText" }))
            Add-Check -Id 'tls-state' -Title 'TLS binding classification' -Category 'tlsState' -Required $false `
                -Status 'SkippedOptional' `
                -FailureReason 'TLS is optional and not discretely surfaced by bootstrapper validate; confirm via netsh http show sslcert when TLS is enabled.'
        }
        catch {
            Add-Check -Id 'binding-validate' -Title 'Bootstrapper binding validation' -Category 'urlAclState' -Required $false `
                -Status 'Warning' -FailureReason "Bootstrapper validate failed: $($_.Exception.Message)"
        }
    }
    else {
        foreach ($cat in @(
                @{ id = 'urlacl-state'; cat = 'urlAclState' },
                @{ id = 'firewall-state'; cat = 'firewallState' },
                @{ id = 'tls-state'; cat = 'tlsState' })) {
            Add-Check -Id $cat.id -Title "Binding classification: $($cat.cat)" -Category $cat.cat -Required $false `
                -Status 'SkippedOptional' -FailureReason 'No -BootstrapperPath supplied; binding state not classified.'
        }
    }
}

# ---------------------------------------------------------------------------
# Mutating install lifecycle gates (authorization-gated). Batch E fills the
# concrete mutation bodies; here they are registered so the certification
# classification honestly reflects which gates were authorized.
# ---------------------------------------------------------------------------

# Apply bindings (service + firewall + URL ACL + TLS) via the bootstrapper's
# holistic repair, at most once per run, then return the parsed validate JSON.
function Invoke-BootstrapperRepairOnce {
    if (-not $BootstrapperPath -or -not (Test-Path -LiteralPath $BootstrapperPath)) { return $null }
    if (-not $script:BindingsApplied) {
        $raw = & $BootstrapperPath repair --json 2>&1
        Set-Content -LiteralPath (Join-Path $script:EvidenceDir 'bootstrapper-repair.json') -Value ($raw | Out-String) -Encoding UTF8
        $script:EvidenceFiles.Add('bootstrapper-repair.json') | Out-Null
        $script:BindingsApplied = $true
    }
    $rawV = & $BootstrapperPath validate --json 2>&1
    try { return (($rawV | Out-String) | ConvertFrom-Json -ErrorAction Stop) } catch { return $null }
}

# Record a binding-apply gate: apply once via the bootstrapper, then classify
# the requested surface from the post-apply validate payload.
function Add-BindingGateResult {
    param([string]$Id, [string]$Title, [string]$Category, [string]$SurfaceField = 'valid')
    if (-not $BootstrapperPath -or -not (Test-Path -LiteralPath $BootstrapperPath)) {
        Add-Check -Id $Id -Title $Title -Category $Category -Status 'Fail' `
            -FailureReason 'Gate authorized but -BootstrapperPath was not provided.' `
            -RemediationHint 'Pass -BootstrapperPath <MasterControlBootstrapper.exe>.'
        return
    }
    $v = Invoke-BootstrapperRepairOnce
    $ok = [bool](Get-McosJsonPath -Json $v -Path $SurfaceField)
    Add-Check -Id $Id -Title $Title -Category $Category -Evidence 'bootstrapper-repair.json' `
        -Status ($(if ($ok) { 'Pass' } else { 'Fail' })) `
        -FailureReason ($(if ($ok) { '' } else { "After apply, the bootstrapper does not report '$SurfaceField' true." })) `
        -RemediationHint 'Run MasterControlBootstrapper.exe repair --json elevated and review the surface.'
}

function Invoke-InstallLifecycleGates {
    # Final build + packaging are performed by the operator's build pipeline,
    # not by this host-side run; when authorized we record that honestly.
    Invoke-AuthorizedGate -Id 'gate-final-build' -Title 'Final build gate' -Category 'installState' `
        -Authorized $AuthorizeFinalBuild -SwitchName 'AuthorizeFinalBuild' -Required $false -Action {
        Add-Check -Id 'gate-final-build' -Title 'Final build (out-of-band)' -Category 'installState' -Required $false `
            -Status 'Warning' -FailureReason 'Authorized; the final build is produced by the operator build pipeline, not this script.'
    }
    Invoke-AuthorizedGate -Id 'gate-package' -Title 'Package gate' -Category 'installState' `
        -Authorized $AuthorizePackage -SwitchName 'AuthorizePackage' -Required $false -Action {
        Add-Check -Id 'gate-package' -Title 'Package (out-of-band)' -Category 'installState' -Required $false `
            -Status 'Warning' -FailureReason 'Authorized; MSI packaging is produced by the operator build pipeline, not this script.'
    }

    Invoke-AuthorizedGate -Id 'gate-msi-install' -Title 'MSI install gate' -Category 'installState' `
        -Authorized $AuthorizeInstall -SwitchName 'AuthorizeInstall' -Action {
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        if (-not $MsiPath -or -not (Test-Path -LiteralPath $MsiPath)) {
            Add-Check -Id 'gate-msi-install' -Title 'MSI install' -Category 'installState' -Status 'Fail' `
                -FailureReason 'AuthorizeInstall set but -MsiPath was not provided or not found.' -RemediationHint 'Pass -MsiPath <.msi>.'
            return
        }
        $log = Join-Path $OutDirectory 'msi-install.log'
        $proc = Start-Process -FilePath 'msiexec.exe' -ArgumentList @('/i', "`"$MsiPath`"", '/qn', '/norestart', '/l*v', "`"$log`"") -Wait -PassThru
        $sw.Stop()
        if (Test-Path -LiteralPath $log) { $script:EvidenceFiles.Add('msi-install.log') | Out-Null }
        $installedDir = if ($InstallDirectory) { $InstallDirectory } else { Join-Path ${env:ProgramFiles} 'Master Control Orchestration Server' }
        $stateOk = Test-Path -LiteralPath (Join-Path $installedDir 'installation-state.json')
        $installOk = (($proc.ExitCode -eq 0 -or $proc.ExitCode -eq 3010) -and $stateOk)
        Add-Check -Id 'gate-msi-install' -Title 'MSI install' -Category 'installState' -DurationMs ([int]$sw.ElapsedMilliseconds) `
            -Status ($(if ($installOk) { 'Pass' } else { 'Fail' })) -Evidence 'msi-install.log' `
            -FailureReason ($(if ($installOk) { '' } else { "msiexec exit $($proc.ExitCode); installation-state.json present=$stateOk." })) `
            -RemediationHint 'Review msi-install.log for the failing action.'
    }

    Invoke-AuthorizedGate -Id 'gate-service-mutation' -Title 'Service start/stop/restart gate' -Category 'serviceState' `
        -Authorized $AuthorizeServiceMutation -SwitchName 'AuthorizeServiceMutation' -Action {
        $svc = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
        if (-not $svc) {
            Add-Check -Id 'gate-service-mutation' -Title 'Service start/stop/restart' -Category 'serviceState' -Status 'Fail' `
                -FailureReason "Service '$ServiceName' is not registered." -RemediationHint 'Install first, or pass -ServiceName.'
            return
        }
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        try {
            if ($svc.Status -ne 'Stopped') { Stop-Service -Name $ServiceName -Force -ErrorAction Stop; Start-Sleep -Seconds 2 }
            Start-Service -Name $ServiceName -ErrorAction Stop
            Start-Sleep -Seconds 3
            $stable = $true
            for ($i = 0; $i -lt 5; $i++) {
                $h = Invoke-McosHttpProbe -Method GET -Url "$BaseUrl/api/health"
                if (-not $h.ok) { $stable = $false; break }
                Start-Sleep -Seconds 2
            }
            $sw.Stop()
            Add-Check -Id 'gate-service-mutation' -Title 'Service restart + health stability' -Category 'serviceState' -DurationMs ([int]$sw.ElapsedMilliseconds) `
                -Status ($(if ($stable) { 'Pass' } else { 'Fail' })) `
                -FailureReason ($(if ($stable) { '' } else { 'Admin /api/health did not stay reachable across the post-restart interval.' }))
        }
        catch {
            $sw.Stop()
            Add-Check -Id 'gate-service-mutation' -Title 'Service start/stop/restart' -Category 'serviceState' -DurationMs ([int]$sw.ElapsedMilliseconds) `
                -Status 'Fail' -FailureReason "Service mutation failed: $($_.Exception.Message)"
        }
    }

    Invoke-AuthorizedGate -Id 'gate-firewall-mutation' -Title 'Firewall rule gate' -Category 'firewallState' `
        -Authorized $AuthorizeFirewallMutation -SwitchName 'AuthorizeFirewallMutation' -Action {
        Add-BindingGateResult -Id 'gate-firewall-mutation' -Title 'Firewall inbound gateway rule applied' -Category 'firewallState' -SurfaceField 'gatewayFirewallRulePresent'
    }
    Invoke-AuthorizedGate -Id 'gate-urlacl-mutation' -Title 'URL ACL gate' -Category 'urlAclState' `
        -Authorized $AuthorizeUrlAclMutation -SwitchName 'AuthorizeUrlAclMutation' -Action {
        Add-BindingGateResult -Id 'gate-urlacl-mutation' -Title 'HTTP.sys bindings applied and valid' -Category 'urlAclState' -SurfaceField 'valid'
    }
    Invoke-AuthorizedGate -Id 'gate-tls-mutation' -Title 'TLS certificate binding gate' -Category 'tlsState' `
        -Authorized $AuthorizeTlsMutation -SwitchName 'AuthorizeTlsMutation' -Required $false -Action {
        Add-BindingGateResult -Id 'gate-tls-mutation' -Title 'Bindings valid after TLS apply' -Category 'tlsState' -SurfaceField 'valid'
    }

    Invoke-AuthorizedGate -Id 'gate-repair' -Title 'Repair gate' -Category 'installState' `
        -Authorized $AllowRepairTest -SwitchName 'AllowRepairTest' -Required $false -Action {
        if (-not $BootstrapperPath -or -not (Test-Path -LiteralPath $BootstrapperPath)) {
            Add-Check -Id 'gate-repair' -Title 'Repair' -Category 'installState' -Required $false -Status 'Fail' `
                -FailureReason 'Repair authorized but -BootstrapperPath not provided.'
            return
        }
        $raw = & $BootstrapperPath repair --json 2>&1
        Set-Content -LiteralPath (Join-Path $OutDirectory 'bootstrapper-repair.log') -Value ($raw | Out-String) -Encoding UTF8
        $script:EvidenceFiles.Add('bootstrapper-repair.log') | Out-Null
        $ok = ($LASTEXITCODE -eq 0)
        Add-Check -Id 'gate-repair' -Title 'Repair completes' -Category 'installState' -Required $false -Evidence 'bootstrapper-repair.log' `
            -Status ($(if ($ok) { 'Pass' } else { 'Fail' })) -FailureReason ($(if ($ok) { '' } else { "Bootstrapper repair exit $LASTEXITCODE." }))
    }

    Invoke-AuthorizedGate -Id 'gate-uninstall' -Title 'Uninstall gate' -Category 'installState' `
        -Authorized $AuthorizeUninstall -SwitchName 'AuthorizeUninstall' -Required $false -Action {
        if (-not $BootstrapperPath -or -not (Test-Path -LiteralPath $BootstrapperPath)) {
            Add-Check -Id 'gate-uninstall' -Title 'Uninstall' -Category 'installState' -Required $false -Status 'Fail' `
                -FailureReason 'Uninstall authorized but -BootstrapperPath not provided.'
            return
        }
        $raw = & $BootstrapperPath uninstall --json 2>&1
        Set-Content -LiteralPath (Join-Path $OutDirectory 'bootstrapper-uninstall.log') -Value ($raw | Out-String) -Encoding UTF8
        $script:EvidenceFiles.Add('bootstrapper-uninstall.log') | Out-Null
        $svcGone = -not (Get-Service -Name $ServiceName -ErrorAction SilentlyContinue)
        $ok = ($LASTEXITCODE -eq 0 -and $svcGone)
        Add-Check -Id 'gate-uninstall' -Title 'Uninstall removes the service' -Category 'installState' -Required $false -Evidence 'bootstrapper-uninstall.log' `
            -Status ($(if ($ok) { 'Pass' } else { 'Fail' })) -FailureReason ($(if ($ok) { '' } else { "Uninstall exit $LASTEXITCODE; service removed=$svcGone." }))
    }

    Invoke-AuthorizedGate -Id 'gate-reinstall' -Title 'Reinstall gate' -Category 'installState' `
        -Authorized $AllowReinstallTest -SwitchName 'AllowReinstallTest' -Required $false -Action {
        if (-not $MsiPath -or -not (Test-Path -LiteralPath $MsiPath)) {
            Add-Check -Id 'gate-reinstall' -Title 'Reinstall' -Category 'installState' -Required $false -Status 'Fail' `
                -FailureReason 'Reinstall authorized but -MsiPath not provided.'
            return
        }
        $log = Join-Path $OutDirectory 'msi-reinstall.log'
        $proc = Start-Process -FilePath 'msiexec.exe' -ArgumentList @('/i', "`"$MsiPath`"", '/qn', '/norestart', 'REINSTALL=ALL', 'REINSTALLMODE=vomus', '/l*v', "`"$log`"") -Wait -PassThru
        if (Test-Path -LiteralPath $log) { $script:EvidenceFiles.Add('msi-reinstall.log') | Out-Null }
        $ok = ($proc.ExitCode -eq 0 -or $proc.ExitCode -eq 3010)
        Add-Check -Id 'gate-reinstall' -Title 'Reinstall completes' -Category 'installState' -Required $false -Evidence 'msi-reinstall.log' `
            -Status ($(if ($ok) { 'Pass' } else { 'Fail' })) -FailureReason ($(if ($ok) { '' } else { "Reinstall msiexec exit $($proc.ExitCode)." }))
    }
}

function Invoke-LanPeerCertification {
    Invoke-AuthorizedGate -Id 'gate-second-host-lan' -Title 'Second-host LAN certification gate' -Category 'clientHeartbeatFreshness' `
        -Authorized $AuthorizeSecondHostLanTest -SwitchName 'AuthorizeSecondHostLanTest' -Action {
        if ($LanPeerReportPath -and (Test-Path -LiteralPath $LanPeerReportPath)) {
            try {
                $peer = Get-Content -LiteralPath $LanPeerReportPath -Raw | ConvertFrom-Json -ErrorAction Stop
                Copy-Item -LiteralPath $LanPeerReportPath -Destination (Join-Path $OutDirectory 'lan-peer-report.json') -Force
                $script:EvidenceFiles.Add('lan-peer-report.json') | Out-Null
                $peerPassed = $false
                if ($peer.PSObject.Properties['passed']) { $peerPassed = [bool]$peer.passed }
                Add-Check -Id 'lan-peer-report' -Title 'Second-host LAN acceptance report is passing' -Category 'clientHeartbeatFreshness' `
                    -Status ($(if ($peerPassed) { 'Pass' } else { 'Fail' })) -Evidence 'lan-peer-report.json' `
                    -FailureReason ($(if ($peerPassed) { '' } else { 'Second-host report is not passing.' }))
            }
            catch {
                Add-Check -Id 'lan-peer-report' -Title 'Second-host LAN acceptance report' -Category 'clientHeartbeatFreshness' `
                    -Status 'Fail' -FailureReason "Could not read LAN peer report: $($_.Exception.Message)"
            }
        }
        else {
            Add-Check -Id 'lan-peer-report' -Title 'Second-host LAN acceptance report' -Category 'clientHeartbeatFreshness' `
                -Status 'Fail' -FailureReason 'Authorized but no -LanPeerReportPath was provided.' `
                -RemediationHint 'Run Test-MasterControlLanClientAcceptance.ps1 on the peer and pass its report via -LanPeerReportPath.'
        }
    }
}

function Invoke-KitPreparation {
    $generator = Join-Path $PSScriptRoot 'New-MasterControlHumanAlphaTestKit.ps1'
    if (-not (Test-Path -LiteralPath $generator)) {
        Add-Check -Id 'kit-generate' -Title 'Human test kit generation' -Category 'onboardingProfiles' -Required $false `
            -Status 'Warning' -FailureReason 'New-MasterControlHumanAlphaTestKit.ps1 not found next to the orchestrator.'
        return
    }
    if (-not $KitOutputDirectory) { $KitOutputDirectory = Join-Path $OutDirectory 'human-test-kit' }
    try {
        & $generator -OutputDirectory $KitOutputDirectory -BaseUrl $BaseUrl -InstallDirectory $InstallDirectory -ClientBundlePath $ClientBundlePath | Out-Null
        $ok = (Test-Path -LiteralPath (Join-Path $KitOutputDirectory 'SECOND-HOST-TEST-CARD.md'))
        Add-Check -Id 'kit-generate' -Title 'Human test kit generated' -Category 'onboardingProfiles' -Required $false `
            -Status ($(if ($ok) { 'Pass' } else { 'Fail' })) -Evidence $KitOutputDirectory `
            -FailureReason ($(if ($ok) { '' } else { 'Kit generation did not produce SECOND-HOST-TEST-CARD.md.' }))
    }
    catch {
        Add-Check -Id 'kit-generate' -Title 'Human test kit generation' -Category 'onboardingProfiles' -Required $false `
            -Status 'Fail' -FailureReason "Kit generator failed: $($_.Exception.Message)"
    }
}

# ===========================================================================
# Mode dispatch
# ===========================================================================

$runFeatureExercise = $Mode -in @('Inspect', 'CertifyLocal', 'CertifyLanPeer', 'FullCertification')

if ($runFeatureExercise) {
    Invoke-AdminApiChecks
    Invoke-WorkingAlphaReadinessChecks
    Invoke-GatewayChecks
    Invoke-PoolChecks
    Invoke-LeaseChecks
    Invoke-DiscoveryChecks
    Invoke-OnboardingChecks
    Invoke-ClientChecks
    Invoke-GovernanceChecks
    Invoke-ConfirmGuardChecks
    Invoke-DiagnosticsChecks
    Invoke-DashboardChecks
    Invoke-WinuiChecks
    Invoke-BindingStateChecks
}

if ($Mode -in @('CertifyLocal', 'FullCertification')) {
    Invoke-InstallLifecycleGates
}
if ($Mode -in @('CertifyLanPeer', 'FullCertification')) {
    Invoke-LanPeerCertification
}
if ($Mode -eq 'PrepareLanPeerKit') {
    Invoke-KitPreparation
}

# ===========================================================================
# Classification + reports + evidence bundle
# ===========================================================================

$requiredFail = @($script:Checks | Where-Object { $_.required -and $_.status -eq 'Fail' }).Count
$requiredMissingAuth = @($script:Checks | Where-Object { $_.required -and $_.status -eq 'NotRunMissingAuthorization' }).Count

$classification =
if ($Mode -eq 'Inspect') { 'InspectionOnly' }
elseif ($Mode -eq 'PrepareLanPeerKit') { 'KitPrepared' }
elseif ($requiredFail -gt 0) { 'NotCertified' }
elseif ($requiredMissingAuth -gt 0) { 'ReadyForOperatorAuthorizedCertification' }
else { 'DeployableHumanTestableWorkingAlpha' }

$summary = [ordered]@{
    total                     = $script:Checks.Count
    pass                      = @($script:Checks | Where-Object { $_.status -eq 'Pass' }).Count
    fail                      = @($script:Checks | Where-Object { $_.status -eq 'Fail' }).Count
    warning                   = @($script:Checks | Where-Object { $_.status -eq 'Warning' }).Count
    skippedOptional           = @($script:Checks | Where-Object { $_.status -eq 'SkippedOptional' }).Count
    notRunMissingAuthorization = @($script:Checks | Where-Object { $_.status -eq 'NotRunMissingAuthorization' }).Count
    requiredFail              = $requiredFail
    requiredMissingAuth       = $requiredMissingAuth
}

$authorization = [ordered]@{
    finalBuild        = [bool]$AuthorizeFinalBuild
    package           = [bool]$AuthorizePackage
    msiInstall        = [bool]$AuthorizeInstall
    serviceMutation   = [bool]$AuthorizeServiceMutation
    firewallMutation  = [bool]$AuthorizeFirewallMutation
    urlAclMutation    = [bool]$AuthorizeUrlAclMutation
    tlsMutation       = [bool]$AuthorizeTlsMutation
    repairTest        = [bool]$AllowRepairTest
    uninstallTest     = [bool]$AuthorizeUninstall
    reinstallTest     = [bool]$AllowReinstallTest
    secondHostLanTest = [bool]$AuthorizeSecondHostLanTest
}

# operator-authorization-record.json when any mutating gate was run.
if ($script:AnyGateRun) {
    $record = [ordered]@{
        schemaVersion    = '1.0'
        product          = 'Master Control Orchestration Server'
        purpose          = 'Working alpha deployment certification'
        operator         = [ordered]@{ name = $OperatorName; role = $OperatorRole; authorizedAtUtc = (Get-McosUtcNow) }
        targetHost       = [ordered]@{ hostName = [System.Net.Dns]::GetHostName(); os = [System.Environment]::OSVersion.VersionString; isCleanHost = $false; notes = '' }
        authorizedGates  = $authorization
        constraints      = [ordered]@{ preserveRuntimeData = $true; allowedMaintenanceWindow = ''; doNotRun = @() }
        operatorConfirmationText = 'I authorize the selected MCOS working-alpha validation gates on the target host.'
        signatureOrInitials = ''
    }
    $record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutDirectory 'operator-authorization-record.json') -Encoding UTF8
    $script:EvidenceFiles.Add('operator-authorization-record.json') | Out-Null
}

$report = [ordered]@{
    schemaVersion = '1.0'
    product       = 'Master Control Orchestration Server'
    classification = $classification
    timestampUtc  = (Get-McosUtcNow)
    host          = [ordered]@{ hostName = [System.Net.Dns]::GetHostName(); baseUrl = $BaseUrl; gatewayUrl = $GatewayUrl }
    mode          = $Mode
    authorization = $authorization
    checks        = @($script:Checks)
    summary       = $summary
    evidenceFiles = @($script:EvidenceFiles | Select-Object -Unique)
}
$reportPath = Join-Path $OutDirectory 'acceptance-report.json'
$report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $reportPath -Encoding UTF8

# ACCEPTANCE-SUMMARY.md
$md = [System.Collections.Generic.List[string]]::new()
$md.Add('# MCOS Working-Alpha Acceptance Summary')
$md.Add('')
$md.Add("- Mode: **$Mode**")
$md.Add("- Classification: **$classification**")
$md.Add("- Host: $($report.host.hostName)  ·  Base: $BaseUrl")
$md.Add("- Generated: $($report.timestampUtc)")
$md.Add('')
$md.Add("Totals: $($summary.pass) pass · $($summary.fail) fail · $($summary.warning) warning · $($summary.skippedOptional) skipped-optional · $($summary.notRunMissingAuthorization) not-run(missing-auth)")
$md.Add('')
$md.Add('| Check | Category | Required | Status | Detail |')
$md.Add('|---|---|---|---|---|')
foreach ($c in $script:Checks) {
    $detail = (("$($c.failureReason)") -replace '\|', '\|')
    $md.Add("| $($c.id) | $($c.category) | $(if ($c.required) { 'yes' } else { 'no' }) | $($c.status) | $detail |")
}
$md.Add('')
if ($classification -eq 'ReadyForOperatorAuthorizedCertification') {
    $md.Add('## Pending operator-authorized gates')
    $md.Add('')
    $md.Add('Runtime checks passed. The following mutating gates were not authorized and must be run on an authorized host to reach DeployableHumanTestableWorkingAlpha:')
    foreach ($c in ($script:Checks | Where-Object { $_.required -and $_.status -eq 'NotRunMissingAuthorization' })) {
        $md.Add("- $($c.title): $($c.remediationHint)")
    }
}
($md -join [Environment]::NewLine) | Set-Content -LiteralPath (Join-Path $OutDirectory 'ACCEPTANCE-SUMMARY.md') -Encoding UTF8

if ($ZipEvidence) {
    $zipPath = Join-Path $OutDirectory 'mcos-working-alpha-evidence.zip'
    if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
    Compress-Archive -Path (Join-Path $OutDirectory '*') -DestinationPath $zipPath -Force
    Write-Host "Evidence zip: $zipPath"
}

Write-Host ""
Write-Host "Classification: $classification"
Write-Host "Report: $reportPath"
Write-Host "Totals: $($summary.pass) pass / $($summary.fail) fail / $($summary.warning) warn / $($summary.notRunMissingAuthorization) missing-auth"

# Exit code: required Fail => 1; required NotRunMissingAuthorization => 2
# (pending operator gates); otherwise 0.
if ($requiredFail -gt 0) { exit 1 }
if ($requiredMissingAuth -gt 0) { exit 2 }
exit 0
