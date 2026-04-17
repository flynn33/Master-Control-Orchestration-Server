# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string[]]$ReportPaths,
    [string]$OutputPath,
    [string]$SummaryPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Read-DeploymentReport {
    param([string]$Path)

    $resolved = (Resolve-Path $Path).Path
    $report = Get-Content $resolved -Raw | ConvertFrom-Json

    $scenarios = @()
    foreach ($scenario in $report.scenarios) {
        $failedExpectations = @($scenario.expectations | Where-Object { -not $_.passed } | ForEach-Object { $_.message })
        $scenarios += [pscustomobject][ordered]@{
            scenario = $scenario.scenario
            succeeded = [bool]$scenario.succeeded
            mutating = if ($scenario.PSObject.Properties.Name -contains "mutating") { [bool]$scenario.mutating } else { $null }
            failedExpectations = $failedExpectations
            expectationCount = @($scenario.expectations).Count
            failedExpectationCount = $failedExpectations.Count
        }
    }

    return [pscustomobject][ordered]@{
        path = $resolved
        generatedAt = $report.generatedAt
        completedAt = if ($report.PSObject.Properties.Name -contains "completedAt") { $report.completedAt } else { $null }
        succeeded = [bool]$report.succeeded
        scenario = $report.scenario
        effectiveScenario = $report.effectiveScenario
        hostLabel = if ($report.PSObject.Properties.Name -contains "hostLabel") { $report.hostLabel } else { "" }
        isAdministrator = [bool]$report.isAdministrator
        managedMutating = [bool]$report.managedMutating
        autoElevateManaged = if ($report.PSObject.Properties.Name -contains "autoElevateManaged") { [bool]$report.autoElevateManaged } else { $false }
        hostCaption = $report.host.caption
        hostVersion = $report.host.version
        hostBuildNumber = $report.host.buildNumber
        machineName = $report.host.machineName
        artifactRoot = if ($report.PSObject.Properties.Name -contains "artifactRoot") { $report.artifactRoot } else { $null }
        bundlePath = if ($report.PSObject.Properties.Name -contains "bundlePath") { $report.bundlePath } else { $null }
        hostDiagnosticsDirectory = if ($report.PSObject.Properties.Name -contains "hostDiagnostics") { $report.hostDiagnostics.directory } else { $null }
        followUpActions = if ($report.PSObject.Properties.Name -contains "followUpActions") { @($report.followUpActions) } else { @() }
        scenarios = $scenarios
    }
}

function New-ComparisonSummary {
    param([object[]]$Reports)

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# Master Control Deployment Report Comparison")
    $lines.Add("")
    $lines.Add("* Compared reports: $($Reports.Count)")
    $lines.Add("")

    foreach ($report in $Reports) {
        $heading = if (-not [string]::IsNullOrWhiteSpace($report.hostLabel)) {
            "$($report.hostLabel) ($($report.machineName))"
        } else {
            $report.machineName
        }
        $lines.Add("## $heading")
        $lines.Add("")
        $lines.Add("* Host: $($report.hostCaption) ($($report.hostVersion) build $($report.hostBuildNumber))")
        if (-not [string]::IsNullOrWhiteSpace($report.hostLabel)) {
            $lines.Add("* Host label: $($report.hostLabel)")
        }
        $lines.Add("* Report: $($report.path)")
        $lines.Add("* Generated: $($report.generatedAt)")
        $lines.Add("* Administrator: $($report.isAdministrator)")
        $lines.Add("* Scenario: $($report.scenario) -> $($report.effectiveScenario)")
        $lines.Add("* Overall result: $(if ($report.succeeded) { 'passed' } else { 'failed' })")
        if (-not [string]::IsNullOrWhiteSpace($report.bundlePath)) {
            $lines.Add("* Bundle: $($report.bundlePath)")
        }
        if (-not [string]::IsNullOrWhiteSpace($report.hostDiagnosticsDirectory)) {
            $lines.Add("* Host diagnostics: $($report.hostDiagnosticsDirectory)")
        }
        $lines.Add("")
        $lines.Add("### Scenarios")
        $lines.Add("")
        foreach ($scenario in $report.scenarios) {
            $lines.Add("* $($scenario.scenario): $(if ($scenario.succeeded) { 'passed' } else { 'failed' })")
            if ($scenario.failedExpectationCount -gt 0) {
                foreach ($failure in $scenario.failedExpectations) {
                    $lines.Add("  - $failure")
                }
            }
        }
        if ($report.followUpActions.Count -gt 0) {
            $lines.Add("")
            $lines.Add("### Follow-Up")
            $lines.Add("")
            foreach ($action in $report.followUpActions) {
                $lines.Add("* $action")
            }
        }
        $lines.Add("")
    }

    return [string]::Join([Environment]::NewLine, $lines)
}

$resolvedReports = @($ReportPaths | ForEach-Object { Read-DeploymentReport -Path $_ })
$comparison = [pscustomobject][ordered]@{
    generatedAt = (Get-Date).ToString("o")
    reportCount = $resolvedReports.Count
    succeeded = (@($resolvedReports | Where-Object { -not $_.succeeded }).Count -eq 0)
    reports = $resolvedReports
}

$comparisonJson = $comparison | ConvertTo-Json -Depth 8

if ($OutputPath) {
    $outputDirectory = Split-Path -Parent $OutputPath
    if ($outputDirectory) {
        New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
    }
    Set-Content -Path $OutputPath -Value $comparisonJson -Encoding UTF8
}

if ($OutputPath -and [string]::IsNullOrWhiteSpace($SummaryPath)) {
    $SummaryPath = [System.IO.Path]::ChangeExtension([System.IO.Path]::GetFullPath($OutputPath), ".md")
}

if ($SummaryPath) {
    $summaryDirectory = Split-Path -Parent $SummaryPath
    if ($summaryDirectory) {
        New-Item -ItemType Directory -Force -Path $summaryDirectory | Out-Null
    }
    Set-Content -Path $SummaryPath -Value (New-ComparisonSummary -Reports $resolvedReports) -Encoding UTF8
}

$comparisonJson
