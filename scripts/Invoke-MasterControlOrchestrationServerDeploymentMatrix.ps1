# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

[CmdletBinding()]
param(
    [string]$BootstrapperPath = "",
    [string]$OutputDirectory = "",
    [string]$BaseName = "deployment-acceptance",
    [string]$HostLabel = "",
    [switch]$AutoElevateManaged,
    [switch]$KeepArtifacts,
    [string]$ManagedReportPath = "",
    [string]$Server2022ReportPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($BootstrapperPath)) {
    $BootstrapperPath = Join-Path $repoRoot "dist\debug\MasterControlBootstrapper.exe"
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot "build\debug"
}

if ([string]::IsNullOrWhiteSpace($HostLabel)) {
    $HostLabel = "local-nonadmin"
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$localReportPath = Join-Path $OutputDirectory ($BaseName + "-local.json")
$comparisonReportPath = Join-Path $OutputDirectory ($BaseName + "-comparison.json")
$testScriptPath = Join-Path $PSScriptRoot "Test-MasterControlOrchestrationServerDeployment.ps1"
$compareScriptPath = Join-Path $PSScriptRoot "Compare-MasterControlOrchestrationServerDeploymentReports.ps1"

$testArguments = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $testScriptPath,
    "-BootstrapperPath", $BootstrapperPath,
    "-Scenario", "auto",
    "-HostLabel", $HostLabel,
    "-ReportPath", $localReportPath
)
if ($AutoElevateManaged) {
    $testArguments += "-AutoElevateManaged"
}
if ($KeepArtifacts) {
    $testArguments += "-KeepArtifacts"
}

$testProcess = Start-Process `
    -FilePath "powershell.exe" `
    -ArgumentList ([string]::Join(" ", ($testArguments | ForEach-Object {
                if ($_ -match "\s") { '"' + $_ + '"' } else { $_ }
            }))) `
    -Wait `
    -PassThru `
    -NoNewWindow
if ($testProcess.ExitCode -ne 0) {
    throw "Deployment acceptance script failed with exit code $($testProcess.ExitCode)."
}

$reportPaths = New-Object System.Collections.ArrayList
[void]$reportPaths.Add($localReportPath)

if ([string]::IsNullOrWhiteSpace($ManagedReportPath)) {
    $ManagedReportPath = [System.IO.Path]::ChangeExtension($localReportPath, ".managed.json")
}
if (Test-Path $ManagedReportPath) {
    [void]$reportPaths.Add((Resolve-Path $ManagedReportPath).Path)
}

if ([string]::IsNullOrWhiteSpace($Server2022ReportPath)) {
    $Server2022ReportPath = [System.IO.Path]::ChangeExtension($localReportPath, ".server2022.json")
}
if (Test-Path $Server2022ReportPath) {
    [void]$reportPaths.Add((Resolve-Path $Server2022ReportPath).Path)
}

$compareArguments = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $compareScriptPath,
    "-ReportPaths"
) + @($reportPaths) + @(
    "-OutputPath", $comparisonReportPath
)

$compareProcess = Start-Process `
    -FilePath "powershell.exe" `
    -ArgumentList ([string]::Join(" ", ($compareArguments | ForEach-Object {
                if ($_ -match "\s") { '"' + $_ + '"' } else { $_ }
            }))) `
    -Wait `
    -PassThru `
    -NoNewWindow
if ($compareProcess.ExitCode -ne 0) {
    throw "Deployment comparison script failed with exit code $($compareProcess.ExitCode)."
}

$result = [pscustomobject][ordered]@{
    generatedAt = (Get-Date).ToString("o")
    bootstrapperPath = (Resolve-Path $BootstrapperPath).Path
    outputDirectory = (Resolve-Path $OutputDirectory).Path
    localReportPath = (Resolve-Path $localReportPath).Path
    comparisonReportPath = (Resolve-Path $comparisonReportPath).Path
    comparisonSummaryPath = [System.IO.Path]::ChangeExtension((Resolve-Path $comparisonReportPath).Path, ".md")
    includedReports = @($reportPaths)
    autoElevateManaged = [bool]$AutoElevateManaged
}

$result | ConvertTo-Json -Depth 5
