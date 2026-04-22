# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

[CmdletBinding()]
param(
    [string]$OutputRoot = "",
    [switch]$Bundle
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

function Get-PersistentInstallerLogRoot {
    return (Join-Path (Get-PersistentLogRoot) "installer")
}

function Get-PersistentLogRoot {
    if (-not [string]::IsNullOrWhiteSpace($env:MASTERCONTROL_BOOTSTRAPPER_PERSISTENT_LOG_DIR)) {
        return (Split-Path -Parent $env:MASTERCONTROL_BOOTSTRAPPER_PERSISTENT_LOG_DIR)
    }

    $publicDocuments = [Environment]::GetFolderPath("CommonDocuments")
    if (-not [string]::IsNullOrWhiteSpace($publicDocuments)) {
        return (Join-Path $publicDocuments "Master Control Orchestration Server\logs")
    }

    if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        return (Join-Path $env:LOCALAPPDATA "Master Control Orchestration Server\logs")
    }

    return (Join-Path $env:TEMP "Master Control Orchestration Server\logs")
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot "build\deployment-diagnostics"
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$destinationRoot = Join-Path $OutputRoot ("deployment-diagnostics-" + $timestamp)
$artifactsDirectory = Join-Path $destinationRoot "artifacts"
New-Item -ItemType Directory -Force -Path $artifactsDirectory | Out-Null

$persistentLogRoot = Get-PersistentLogRoot
$persistentInstallerLogRoot = Get-PersistentInstallerLogRoot
$latestSessionPath = Join-Path $persistentInstallerLogRoot "latest-session.json"
$latestRecordPath = Join-Path $persistentInstallerLogRoot "installer-latest.json"
$latestFailurePath = Join-Path $persistentInstallerLogRoot "installer-latest-failure.json"
$shellLatestPath = Join-Path $persistentInstallerLogRoot "components\shell-latest.log"
$serviceLatestPath = Join-Path $persistentInstallerLogRoot "components\service-latest.log"
$logLocationPath = Join-Path $persistentLogRoot "LOG-LOCATION.txt"
$runtimeLogLocationPath = Join-Path $persistentLogRoot "runtime\LOG-LOCATION.txt"
$runtimeEventsPath = Join-Path $persistentLogRoot "runtime\events.jsonl"
$runtimeTelemetryPath = Join-Path $persistentLogRoot "runtime\telemetry.jsonl"
$shellLogLocationPath = Join-Path $persistentLogRoot "shell\LOG-LOCATION.txt"
$shellEventsPath = Join-Path $persistentLogRoot "shell\events.jsonl"
$shellTelemetryPath = Join-Path $persistentLogRoot "shell\telemetry.jsonl"

$summary = [ordered]@{
    generatedAt = (Get-Date).ToString("o")
    persistentLogRoot = $persistentLogRoot
    persistentInstallerLogRoot = $persistentInstallerLogRoot
    latestSessionPath = $latestSessionPath
    latestRecordPath = $latestRecordPath
    latestFailurePath = $latestFailurePath
    logLocationPath = $logLocationPath
    runtimeLogLocationPath = $runtimeLogLocationPath
    runtimeEventsPath = $runtimeEventsPath
    runtimeTelemetryPath = $runtimeTelemetryPath
    shellLogLocationPath = $shellLogLocationPath
    shellEventsPath = $shellEventsPath
    shellTelemetryPath = $shellTelemetryPath
    shellLatestPath = $shellLatestPath
    serviceLatestPath = $serviceLatestPath
    copiedArtifacts = @()
}

foreach ($candidatePath in @(
    $logLocationPath,
    $runtimeLogLocationPath,
    $runtimeEventsPath,
    $runtimeTelemetryPath,
    $shellLogLocationPath,
    $shellEventsPath,
    $shellTelemetryPath,
    $latestSessionPath,
    $latestRecordPath,
    $latestFailurePath,
    $shellLatestPath,
    $serviceLatestPath
)) {
    if (Test-Path $candidatePath) {
        $nameParts = @()
        $parent = Split-Path -Parent $candidatePath
        if (-not [string]::IsNullOrWhiteSpace($parent)) {
            $nameParts += (Split-Path -Leaf $parent)
        }
        $nameParts += ([System.IO.Path]::GetFileName($candidatePath))
        $destinationPath = Join-Path $artifactsDirectory ($nameParts -join "-")
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
$lines.Add("* Root log location note: $($summary.logLocationPath)")
$lines.Add("* Runtime events: $($summary.runtimeEventsPath)")
$lines.Add("* Runtime telemetry: $($summary.runtimeTelemetryPath)")
$lines.Add("* Shell events: $($summary.shellEventsPath)")
$lines.Add("* Shell telemetry: $($summary.shellTelemetryPath)")
$lines.Add("* Installer root: $($summary.persistentInstallerLogRoot)")
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
