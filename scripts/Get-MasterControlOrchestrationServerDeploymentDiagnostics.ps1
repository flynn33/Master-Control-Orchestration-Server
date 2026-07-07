# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

[CmdletBinding()]
param(
    [string]$OutputRoot = "",
    [string]$BaseUrl = "",
    [string]$GatewayUrl = "",
    [string]$InstallDirectory = "",
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

# ---------------------------------------------------------------------------
# Host runtime diagnostics (working-alpha). Captures service, event log, HTTP.sys
# binding, firewall, port-listener, and live endpoint/gateway probe evidence so
# an install/runtime/LAN failure is diagnosable without guessing. Every collector
# is isolated: a single failure records status "error"/"unknown" and never aborts
# the run. Collection is read-only; it never mutates host state. Runs identically
# after a passing or a failing acceptance attempt.
# ---------------------------------------------------------------------------
$hostDir = Join-Path $artifactsDirectory "_host"
New-Item -ItemType Directory -Force -Path $hostDir | Out-Null
$hostDiag = [ordered]@{}

function Write-DiagArtifact {
    param([string]$Name, [string]$Content)
    $path = Join-Path $hostDir $Name
    Set-Content -LiteralPath $path -Value $Content -Encoding UTF8
    return $path
}

function Invoke-DiagCollector {
    param([string]$Label, [scriptblock]$Script)
    $entry = [ordered]@{ status = "unknown"; artifact = $null; note = "" }
    try {
        $output = & $Script 2>&1 | Out-String
        $entry.artifact = Write-DiagArtifact -Name "$Label.txt" -Content $output
        $entry.status = "collected"
    } catch {
        $entry.status = "error"
        $entry.note = $_.Exception.Message
    }
    $hostDiag[$Label] = $entry
}

# Host + service state.
Invoke-DiagCollector -Label "host-context" -Script {
    Get-CimInstance Win32_OperatingSystem -ErrorAction SilentlyContinue |
        Format-List Caption, Version, OSArchitecture, CSName
    "IsAdministrator: " + ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
Invoke-DiagCollector -Label "service-status" -Script {
    Get-Service -Name MasterControlProgram -ErrorAction SilentlyContinue | Format-List *
    Get-CimInstance Win32_Service -Filter "Name='MasterControlProgram'" -ErrorAction SilentlyContinue |
        Format-List Name, State, StartMode, StartName, ProcessId, PathName
}
Invoke-DiagCollector -Label "service-events" -Script {
    Get-WinEvent -FilterHashtable @{ LogName = 'System'; ProviderName = 'Service Control Manager' } -MaxEvents 200 -ErrorAction SilentlyContinue |
        Where-Object { $_.Message -match 'MasterControlProgram|Master Control' } |
        Format-List TimeCreated, Id, LevelDisplayName, Message
}

# HTTP.sys bindings (read-only netsh queries).
Invoke-DiagCollector -Label "http-urlacl" -Script { netsh.exe http show urlacl }
Invoke-DiagCollector -Label "http-sslcert" -Script { netsh.exe http show sslcert }
Invoke-DiagCollector -Label "firewall-rules" -Script {
    Get-NetFirewallRule -DisplayName 'Master Control Orchestration Server*' -ErrorAction SilentlyContinue |
        ForEach-Object {
            $ports = ($_ | Get-NetFirewallPortFilter -ErrorAction SilentlyContinue)
            [PSCustomObject]@{
                DisplayName = $_.DisplayName
                Enabled     = $_.Enabled
                Direction   = $_.Direction
                Action      = $_.Action
                Profile     = $_.Profile
                Protocol    = $ports.Protocol
                LocalPort   = ($ports.LocalPort -join ',')
            }
        } | Format-List *
}
Invoke-DiagCollector -Label "port-listeners" -Script {
    Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue |
        Sort-Object LocalPort | Format-Table -AutoSize LocalAddress, LocalPort, OwningProcess
    "--- netstat -ano (listening) ---"
    netstat.exe -ano | Select-String -Pattern 'LISTENING'
}

# Install-state JSON.
$installStateCandidates = @()
if ($InstallDirectory) { $installStateCandidates += (Join-Path $InstallDirectory 'installation-state.json') }
$installStateCandidates += (Join-Path ${env:ProgramFiles} 'Master Control Orchestration Server\installation-state.json')
$installStateEntry = [ordered]@{ status = "notFound"; artifact = $null; note = "" }
foreach ($candidate in $installStateCandidates) {
    if ($candidate -and (Test-Path -LiteralPath $candidate)) {
        try {
            $installStateEntry.artifact = Join-Path $hostDir 'installation-state.json'
            Copy-Item -LiteralPath $candidate -Destination $installStateEntry.artifact -Force
            $installStateEntry.status = "collected"
            $installStateEntry.note = $candidate
        } catch {
            $installStateEntry.status = "error"; $installStateEntry.note = $_.Exception.Message
        }
        break
    }
}
$hostDiag["install-state"] = $installStateEntry

# Live endpoint + gateway probes (best-effort; absence is recorded, not fatal).
$commonHelper = Join-Path $PSScriptRoot 'MasterControlAcceptanceCommon.ps1'
if (Test-Path -LiteralPath $commonHelper) {
    . $commonHelper
    if (-not $BaseUrl) { $BaseUrl = Resolve-McosAdminBaseUrlFromInstallState -InstallDirectory $InstallDirectory }
    if ($BaseUrl) {
        $BaseUrl = $BaseUrl.TrimEnd('/')
        foreach ($probePath in @('/api/health', '/api/health/summary', '/api/gateway/status', '/api/gateway/health', '/api/telemetry/clients', '/api/discovery')) {
            $probe = Invoke-McosHttpProbe -Method GET -Url "$BaseUrl$probePath"
            $label = "probe_" + ($probePath -replace '[^\w]', '_')
            Write-DiagArtifact -Name "$label.json" -Content (($probe | ConvertTo-Json -Depth 8)) | Out-Null
            $hostDiag[$label] = [ordered]@{ status = if ($probe.ok) { "ok" } else { "unreachable" }; statusCode = $probe.statusCode }
        }
        if (-not $GatewayUrl) {
            $discProbe = Invoke-McosHttpProbe -Method GET -Url "$BaseUrl/api/discovery"
            if ($discProbe.jsonValid) { $GatewayUrl = "$(Get-McosJsonPath -Json $discProbe.json -Path 'gateway.mcpUrl')" }
        }
        if ($GatewayUrl) {
            $gwHost = Get-McosUrlHost -Url $GatewayUrl
            if ((Test-McosHostWildcard -HostName $gwHost) -or [string]::IsNullOrEmpty($gwHost)) {
                $GatewayUrl = $GatewayUrl -replace [regex]::Escape($gwHost), '127.0.0.1'
            }
            $mcp = Invoke-McosMcpRpc -GatewayUrl $GatewayUrl -RpcMethod 'initialize' -RpcParams @{ protocolVersion = '2025-03-26'; capabilities = @{}; clientInfo = @{ name = 'mcos-diagnostics'; version = '1.0' } } -Id 1
            Write-DiagArtifact -Name "probe_mcp_initialize.json" -Content (($mcp | ConvertTo-Json -Depth 8)) | Out-Null
            $hostDiag["probe_mcp_initialize"] = [ordered]@{ status = if ($mcp.ok) { "ok" } else { "unreachable" }; statusCode = $mcp.statusCode }
        }
    } else {
        $hostDiag["probes"] = [ordered]@{ status = "skipped"; note = "no admin base URL (pass -BaseUrl or install MCOS)" }
    }
} else {
    $hostDiag["probes"] = [ordered]@{ status = "skipped"; note = "MasterControlAcceptanceCommon.ps1 not found" }
}

$summary.hostDiagnosticsDir = $hostDir
$summary.hostDiagnostics = $hostDiag

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

$lines.Add("## Host Runtime Diagnostics")
$lines.Add("")
foreach ($key in $hostDiag.Keys) {
    $entry = $hostDiag[$key]
    $status = if (($entry -is [System.Collections.IDictionary]) -and $entry.Contains('status')) { $entry['status'] } else { 'collected' }
    $lines.Add("* $key : $status")
}
$lines.Add("")

Set-Content -Path $summaryJsonPath -Value ($summary | ConvertTo-Json -Depth 10) -Encoding UTF8
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
    hostDiagnosticsDir = $hostDir
} | ConvertTo-Json -Depth 5
