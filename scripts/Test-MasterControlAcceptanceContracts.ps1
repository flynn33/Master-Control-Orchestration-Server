# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

<#
.SYNOPSIS
    Contract tests for the working-alpha acceptance orchestrator.

.DESCRIPTION
    Exercises Invoke-MasterControlWorkingAlphaAcceptance.ps1 against a dead port
    (no MCOS running) and asserts the invariants the evidence bundle schema and
    the certification model require:
      - acceptance-report.json carries every required top-level field.
      - every check carries every required field and a valid status value.
      - all 21 evidence categories are represented.
      - Inspect classifies InspectionOnly and exits nonzero on required failure.
      - CertifyLocal without authorization switches classifies NotCertified and
        records every install lifecycle gate as NotRunMissingAuthorization
        (never a pass).

    Non-mutating: it only runs the orchestrator in read modes against a dead
    port and inspects the JSON it writes. Exit code is nonzero if any assertion
    fails, so it can gate CI.
#>

[CmdletBinding()]
param(
    [string]$OrchestratorPath,
    [int]$DeadPort = 59999
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $OrchestratorPath) {
    $OrchestratorPath = Join-Path $PSScriptRoot 'Invoke-MasterControlWorkingAlphaAcceptance.ps1'
}
if (-not (Test-Path -LiteralPath $OrchestratorPath)) {
    Write-Error "Orchestrator not found: $OrchestratorPath"; exit 3
}

$script:failCount = 0
function Assert-That {
    param([bool]$Condition, [string]$Message)
    if ($Condition) { Write-Host "  PASS  $Message" }
    else { Write-Host "  FAIL  $Message" -ForegroundColor Red; $script:failCount++ }
}

$requiredTop = @('schemaVersion', 'product', 'classification', 'timestampUtc', 'host', 'mode', 'authorization', 'checks', 'summary', 'evidenceFiles')
$requiredCheckFields = @('id', 'title', 'category', 'required', 'status', 'timestampUtc', 'durationMs', 'evidence', 'failureReason', 'remediationHint')
$requiredCategories = @('installState', 'serviceState', 'processListenerState', 'urlAclState', 'firewallState', 'tlsState',
    'adminApiProbes', 'gatewayJsonRpcProbes', 'mcpToolCallProbes', 'poolReadiness', 'leaseBehavior', 'discoveryDocuments',
    'onboardingProfiles', 'clientRegistration', 'clientHeartbeatFreshness', 'governanceDecisions', 'confirmGuards',
    'diagnostics', 'workingAlphaReadiness', 'browserDashboard', 'winuiShell')
$validStatuses = @('Pass', 'Fail', 'Warning', 'SkippedOptional', 'NotRunMissingAuthorization')

function Invoke-Run {
    param([string]$Mode)
    $dir = Join-Path ([System.IO.Path]::GetTempPath()) ("mcos-acc-test-" + [Guid]::NewGuid().ToString('N'))
    & pwsh -NoProfile -ExecutionPolicy Bypass -File $OrchestratorPath -Mode $Mode `
        -BaseUrl "http://127.0.0.1:$DeadPort" -GatewayUrl "http://127.0.0.1:$($DeadPort - 1)" -OutDirectory $dir *> $null
    $exitCode = $LASTEXITCODE
    $reportPath = Join-Path $dir 'acceptance-report.json'
    $report = if (Test-Path -LiteralPath $reportPath) { Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json } else { $null }
    return [ordered]@{
        report        = $report
        exitCode      = $exitCode
        dir           = $dir
        summaryExists = (Test-Path -LiteralPath (Join-Path $dir 'ACCEPTANCE-SUMMARY.md'))
    }
}

Write-Host "== Inspect mode: schema + classification =="
$inspect = Invoke-Run -Mode 'Inspect'
Assert-That ($null -ne $inspect.report) 'Inspect wrote acceptance-report.json'
if ($inspect.report) {
    foreach ($f in $requiredTop) { Assert-That ([bool]$inspect.report.PSObject.Properties[$f]) "top-level field '$f' present" }
    Assert-That ($inspect.report.schemaVersion -eq '1.0') 'schemaVersion is 1.0'
    Assert-That ($inspect.report.classification -eq 'InspectionOnly') 'Inspect classifies InspectionOnly'
    Assert-That ($inspect.summaryExists) 'ACCEPTANCE-SUMMARY.md written'

    $categoriesPresent = @($inspect.report.checks | ForEach-Object { $_.category } | Sort-Object -Unique)
    foreach ($c in $requiredCategories) { Assert-That ($categoriesPresent -contains $c) "evidence category '$c' present" }

    $badFieldCheck = $null
    $badStatusCheck = $null
    foreach ($chk in $inspect.report.checks) {
        foreach ($f in $requiredCheckFields) { if (-not $chk.PSObject.Properties[$f]) { $badFieldCheck = "$($chk.id):$f"; break } }
        if ($validStatuses -notcontains $chk.status) { $badStatusCheck = "$($chk.id):$($chk.status)" }
    }
    Assert-That ($null -eq $badFieldCheck) "every check carries every required field ($badFieldCheck)"
    Assert-That ($null -eq $badStatusCheck) "every check uses a valid status ($badStatusCheck)"
}
Assert-That ($inspect.exitCode -eq 1) 'Inspect vs dead port exits 1 (required failure)'

Write-Host "== CertifyLocal: gates fail closed without authorization =="
$certify = Invoke-Run -Mode 'CertifyLocal'
if ($certify.report) {
    Assert-That ($certify.report.classification -eq 'NotCertified') 'CertifyLocal vs dead port classifies NotCertified'
    $gates = @($certify.report.checks | Where-Object { $_.id -like 'gate-*' })
    Assert-That ($gates.Count -ge 10) "install lifecycle gates registered ($($gates.Count))"
    $notRun = @($gates | Where-Object { $_.status -eq 'NotRunMissingAuthorization' })
    Assert-That ($notRun.Count -eq $gates.Count) 'every gate is NotRunMissingAuthorization without its switch'
    $anyPass = @($gates | Where-Object { $_.status -eq 'Pass' })
    Assert-That ($anyPass.Count -eq 0) 'no gate passes without authorization'
}

foreach ($run in @($inspect, $certify)) {
    if ($run.dir -and (Test-Path -LiteralPath $run.dir)) { Remove-Item -LiteralPath $run.dir -Recurse -Force -ErrorAction SilentlyContinue }
}

Write-Host ''
if ($script:failCount -gt 0) {
    Write-Host "FAILED: $($script:failCount) assertion(s)." -ForegroundColor Red
    exit 1
}
Write-Host 'All acceptance-contract assertions passed.' -ForegroundColor Green
exit 0
