# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

[CmdletBinding()]
param(
    [ValidateSet("debug", "release")]
    [string]$Preset = "release",
    [ValidateSet("mixed", "managed", "both")]
    [string]$Scenario = "mixed",
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [switch]$ManagedMutating,
    [switch]$KeepArtifacts,
    [string]$HostLabel = "",
    [string]$ReportPath = "",
    [string]$SummaryPath = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$packageScriptPath = Join-Path $PSScriptRoot "Package-MasterControlOrchestrationServer.ps1"
$acceptanceScriptPath = Join-Path $PSScriptRoot "Test-MasterControlOrchestrationServerDeployment.ps1"
$outputRoot = Join-Path $repoRoot ("dist\packages\" + $Preset)

function Test-IsAdministrator {
    try {
        $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
        $principal = [Security.Principal.WindowsPrincipal]::new($identity)
        return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    } catch {
        return $false
    }
}

function Invoke-ScriptCapture {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ScriptPath,
        [string[]]$Arguments = @()
    )

    $stdout = [System.IO.Path]::GetTempFileName()
    $stderr = [System.IO.Path]::GetTempFileName()

    try {
        $process = Start-Process `
            -FilePath "powershell.exe" `
            -ArgumentList (@("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $ScriptPath) + $Arguments) `
            -Wait `
            -PassThru `
            -NoNewWindow `
            -RedirectStandardOutput $stdout `
            -RedirectStandardError $stderr

        return [pscustomobject]@{
            exitCode = $process.ExitCode
            stdout = (Get-Content $stdout -Raw)
            stderr = (Get-Content $stderr -Raw)
        }
    } finally {
        if (Test-Path $stdout) { Remove-Item $stdout -Force }
        if (Test-Path $stderr) { Remove-Item $stderr -Force }
    }
}

function ConvertFrom-TrailingJson {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Text
    )

    $lines = $Text -split "`r?`n"
    for ($index = $lines.Count - 1; $index -ge 0; $index--) {
        $candidateStart = $lines[$index].TrimStart()
        if (-not ($candidateStart.StartsWith("{") -or $candidateStart.StartsWith("["))) {
            continue
        }

        $candidate = ($lines[$index..($lines.Count - 1)] -join [Environment]::NewLine).Trim()
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }

        try {
            return ($candidate | ConvertFrom-Json)
        } catch {
        }
    }

    throw "The script output did not end with a JSON object."
}

function Format-ProcessFailure {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Prefix,
        [Parameter(Mandatory = $true)]
        [pscustomobject]$Result
    )

    $stdout = if ([string]::IsNullOrWhiteSpace($Result.stdout)) { "<empty>" } else { $Result.stdout.Trim() }
    $stderr = if ([string]::IsNullOrWhiteSpace($Result.stderr)) { "<empty>" } else { $Result.stderr.Trim() }
    return "$Prefix`nSTDOUT:`n$stdout`nSTDERR:`n$stderr"
}

New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

$resolvedReportPath = if ([string]::IsNullOrWhiteSpace($ReportPath)) {
    Join-Path $outputRoot ("ide-acceptance-" + $Scenario + ".json")
} else {
    [System.IO.Path]::GetFullPath($ReportPath)
}

$resolvedSummaryPath = if ([string]::IsNullOrWhiteSpace($SummaryPath)) {
    [System.IO.Path]::ChangeExtension($resolvedReportPath, ".md")
} else {
    [System.IO.Path]::GetFullPath($SummaryPath)
}

$resolvedHostLabel = if ([string]::IsNullOrWhiteSpace($HostLabel)) {
    "ide-" + $Preset + "-" + $Scenario
} else {
    $HostLabel
}

$requiresAdministrator = $ManagedMutating -and ($Scenario -in @("managed", "both"))
if ($requiresAdministrator -and -not (Test-IsAdministrator)) {
    throw "Scenario '$Scenario' with -ManagedMutating must be run from an elevated Visual Studio or Visual Studio Code session."
}

$packageArguments = @("-Preset", $Preset)
if ($SkipBuild) { $packageArguments += "-SkipBuild" }
if ($SkipTests) { $packageArguments += "-SkipTests" }

$packageResult = Invoke-ScriptCapture -ScriptPath $packageScriptPath -Arguments $packageArguments
if ($packageResult.exitCode -ne 0) {
    throw (Format-ProcessFailure -Prefix "Packaging failed with exit code $($packageResult.exitCode)." -Result $packageResult)
}

$packageMetadata = ConvertFrom-TrailingJson -Text $packageResult.stdout
if (-not ($packageMetadata.PSObject.Properties.Name -contains "bootstrapperPath")) {
    throw "Packaging output did not include bootstrapperPath."
}
if (-not ($packageMetadata.PSObject.Properties.Name -contains "setupPath")) {
    throw "Packaging output did not include setupPath."
}

$acceptanceArguments = @(
    "-BootstrapperPath", $packageMetadata.bootstrapperPath,
    "-SetupPath", $packageMetadata.setupPath,
    "-Scenario", $Scenario,
    "-HostLabel", $resolvedHostLabel,
    "-ReportPath", $resolvedReportPath,
    "-SummaryPath", $resolvedSummaryPath
)
if ($ManagedMutating) { $acceptanceArguments += "-ManagedMutating" }
if ($KeepArtifacts) { $acceptanceArguments += "-KeepArtifacts" }

$acceptanceResult = Invoke-ScriptCapture -ScriptPath $acceptanceScriptPath -Arguments $acceptanceArguments
if ($acceptanceResult.exitCode -ne 0) {
    throw (Format-ProcessFailure -Prefix "Deployment acceptance failed to execute with exit code $($acceptanceResult.exitCode)." -Result $acceptanceResult)
}

if (-not (Test-Path $resolvedReportPath)) {
    throw "Deployment acceptance did not produce the expected report at '$resolvedReportPath'."
}

$acceptanceReport = Get-Content $resolvedReportPath -Raw | ConvertFrom-Json
$resultSummary = [pscustomobject][ordered]@{
    preset = $Preset
    scenario = $Scenario
    hostLabel = $resolvedHostLabel
    packageMetadataPath = $packageMetadata.packageMetadataPath
    bootstrapperPath = $packageMetadata.bootstrapperPath
    setupPath = $packageMetadata.setupPath
    reportPath = $resolvedReportPath
    summaryPath = $resolvedSummaryPath
    succeeded = [bool]$acceptanceReport.succeeded
}
$resultSummary | ConvertTo-Json -Depth 6

if (-not $acceptanceReport.succeeded) {
    throw "Deployment acceptance reported failures. See '$resolvedReportPath' and '$resolvedSummaryPath'."
}
