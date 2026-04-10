# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

[CmdletBinding()]
param(
    [string]$OutputRoot = "",
    [switch]$Bundle
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

function Get-PersistentInstallerLogRoot {
    if (-not [string]::IsNullOrWhiteSpace($env:MASTERCONTROL_BOOTSTRAPPER_PERSISTENT_LOG_DIR)) {
        return $env:MASTERCONTROL_BOOTSTRAPPER_PERSISTENT_LOG_DIR
    }

    $publicDocuments = [Environment]::GetFolderPath("CommonDocuments")
    if (-not [string]::IsNullOrWhiteSpace($publicDocuments)) {
        return (Join-Path $publicDocuments "Master Control Orchestration Server\logs\installer")
    }

    if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        return (Join-Path $env:LOCALAPPDATA "Master Control Orchestration Server\logs\installer")
    }

    return (Join-Path $env:TEMP "Master Control Orchestration Server\logs\installer")
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot "build\deployment-diagnostics"
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$destinationRoot = Join-Path $OutputRoot ("deployment-diagnostics-" + $timestamp)
$artifactsDirectory = Join-Path $destinationRoot "artifacts"
New-Item -ItemType Directory -Force -Path $artifactsDirectory | Out-Null

$persistentLogRoot = Get-PersistentInstallerLogRoot
$latestSessionPath = Join-Path $persistentLogRoot "latest-session.json"
$latestRecordPath = Join-Path $persistentLogRoot "installer-latest.json"
$latestFailurePath = Join-Path $persistentLogRoot "installer-latest-failure.json"
$shellLatestPath = Join-Path $persistentLogRoot "components\shell-latest.log"
$serviceLatestPath = Join-Path $persistentLogRoot "components\service-latest.log"

$summary = [ordered]@{
    generatedAt = (Get-Date).ToString("o")
    persistentLogRoot = $persistentLogRoot
    latestSessionPath = $latestSessionPath
    latestRecordPath = $latestRecordPath
    latestFailurePath = $latestFailurePath
    shellLatestPath = $shellLatestPath
    serviceLatestPath = $serviceLatestPath
    copiedArtifacts = @()
}

foreach ($candidatePath in @($latestSessionPath, $latestRecordPath, $latestFailurePath, $shellLatestPath, $serviceLatestPath)) {
    if (Test-Path $candidatePath) {
        $destinationPath = Join-Path $artifactsDirectory ([System.IO.Path]::GetFileName($candidatePath))
        Copy-Item -Path $candidatePath -Destination $destinationPath -Force
        $summary.copiedArtifacts += $destinationPath
    }
}

if (Test-Path $latestSessionPath) {
    try {
        $latestSession = Get-Content $latestSessionPath -Raw | ConvertFrom-Json
        $summary.latestSession = $latestSession
        if ($latestSession.PSObject.Properties.Name -contains "sessionDirectory" -and
            -not [string]::IsNullOrWhiteSpace($latestSession.sessionDirectory) -and
            (Test-Path $latestSession.sessionDirectory)) {
            $sessionCopyRoot = Join-Path $artifactsDirectory "session"
            Copy-Item -Path $latestSession.sessionDirectory -Destination $sessionCopyRoot -Recurse -Force
            $summary.sessionDirectory = $latestSession.sessionDirectory
            $summary.sessionArtifacts = $sessionCopyRoot
        }
    } catch {
        $summary.latestSessionReadError = $_.Exception.Message
    }
}

$summaryJsonPath = Join-Path $destinationRoot "deployment-diagnostics-summary.json"
$summaryMarkdownPath = Join-Path $destinationRoot "deployment-diagnostics-summary.md"

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("# Master Control Orchestration Server Deployment Diagnostics")
$lines.Add("")
$lines.Add("* Generated: $($summary.generatedAt)")
$lines.Add("* Persistent root: $($summary.persistentLogRoot)")
$lines.Add("* Latest session path: $($summary.latestSessionPath)")
$lines.Add("* Latest installer record: $($summary.latestRecordPath)")
$lines.Add("* Latest installer failure: $($summary.latestFailurePath)")
$lines.Add("* Shell latest log: $($summary.shellLatestPath)")
$lines.Add("* Service latest log: $($summary.serviceLatestPath)")
$lines.Add("")

if ($summary.Contains("latestSession")) {
    $lines.Add("## Latest Session")
    $lines.Add("")
    $lines.Add("* Run ID: $($summary.latestSession.runId)")
    $lines.Add("* Component: $($summary.latestSession.component)")
    $lines.Add("* Action: $($summary.latestSession.action)")
    $lines.Add("* Outcome: $($summary.latestSession.outcome)")
    $lines.Add("* Succeeded: $($summary.latestSession.succeeded)")
    $lines.Add("* Message: $($summary.latestSession.message)")
    if ($summary.Contains("sessionDirectory")) {
        $lines.Add("* Session directory: $($summary.sessionDirectory)")
    }
    $lines.Add("")
}

if ($summary.copiedArtifacts.Count -gt 0) {
    $lines.Add("## Copied Artifacts")
    $lines.Add("")
    foreach ($artifact in $summary.copiedArtifacts) {
        $lines.Add("* $artifact")
    }
    if ($summary.Contains("sessionArtifacts")) {
        $lines.Add("* $($summary.sessionArtifacts)")
    }
    $lines.Add("")
}

Set-Content -Path $summaryJsonPath -Value ($summary | ConvertTo-Json -Depth 8) -Encoding UTF8
Set-Content -Path $summaryMarkdownPath -Value ($lines -join [Environment]::NewLine) -Encoding UTF8

$bundlePath = $null
if ($Bundle) {
    $bundlePath = Join-Path $OutputRoot ("deployment-diagnostics-" + $timestamp + ".zip")
    if (Test-Path $bundlePath) {
        Remove-Item -Path $bundlePath -Force
    }
    Compress-Archive -Path $destinationRoot -DestinationPath $bundlePath -Force
}

[pscustomobject]@{
    outputRoot = $destinationRoot
    summaryJsonPath = $summaryJsonPath
    summaryMarkdownPath = $summaryMarkdownPath
    bundlePath = $bundlePath
    persistentLogRoot = $persistentLogRoot
} | ConvertTo-Json -Depth 5
