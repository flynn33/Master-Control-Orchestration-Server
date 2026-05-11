# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

<#
.SYNOPSIS
    Hot-deploy a freshly-built MCOS into the live service path.

.DESCRIPTION
    `cmake --install` writes a staged install to
    `C:\Program Files\MasterControlOrchestrationServer\` (no spaces). The
    Windows service binary that the SCM points at lives at
    `C:\Program Files\Master Control Orchestration Server\` (with spaces),
    because the WiX MSI installer (MasterControlOrchestrationServerSetup.exe)
    is the canonical deployment path and uses the spaces-path layout.

    The two paths do not auto-reconcile. During development, a freshly-built
    binary cannot be picked up by the running service just by running
    `cmake --install` — the service has to be stopped, the new binary
    copied into the spaces-path, and the service restarted.

    This script handles that round-trip:
      1. Stops the MasterControlProgram service.
      2. Stops any running MasterControlShell.exe processes.
      3. Copies the freshly-built service binary, shell binary, shell PDB
         (if present), web resources (app.js + styles.css), and Forsetti
         manifests from the build tree into the spaces-path live install.
      4. Restarts the service and waits for `/api/version` to respond.
      5. Optionally relaunches the shell.

.PARAMETER BuildDir
    Path to the CMake build directory. Defaults to `build\debug` relative
    to the repository root.

.PARAMETER RelaunchShell
    Pass to relaunch MasterControlShell.exe after the service comes back.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass `
        -File scripts\Deploy-LocalLive.ps1 -RelaunchShell

.NOTES
    Requires Administrator. The Stop-Service / Start-Service calls and
    the writes under `C:\Program Files\...` will fail without elevation.
#>

[CmdletBinding()]
param(
    [string]$BuildDir,
    [switch]$RelaunchShell
)

$ErrorActionPreference = 'Stop'

# Resolve the script's own directory. $PSScriptRoot can be empty when
# invoked through certain PowerShell host configurations; fall back to
# $MyInvocation.MyCommand.Path so the script works under both `&` and
# `-File` invocation styles.
$ScriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$RepoRoot  = Split-Path -Parent $ScriptDir
if (-not $BuildDir) {
    $BuildDir = Join-Path $RepoRoot 'build\debug'
}

$LivePath = 'C:\Program Files\Master Control Orchestration Server'
$LiveWebRoot = Join-Path $LivePath 'share\MasterControlOrchestrationServer\web'
$ServiceName = 'MasterControlProgram'

# Resolve the source artifacts from the build tree.
$ServiceHostSource = Join-Path $BuildDir 'src\MasterControlServiceHost\Debug\MasterControlServiceHost.exe'
$ShellSource      = Join-Path $BuildDir 'src\MasterControlShell\Debug\MasterControlShell.exe'
$ShellPdbSource   = Join-Path $BuildDir 'src\MasterControlShell\Debug\MasterControlShell.pdb'
$AppJsSource      = Join-Path $RepoRoot 'resources\web\app.js'
$StylesCssSource  = Join-Path $RepoRoot 'resources\web\styles.css'
$IndexHtmlSource  = Join-Path $RepoRoot 'resources\web\index.html'

$required = @(
    @{ Name = 'ServiceHost'; Path = $ServiceHostSource },
    @{ Name = 'Shell';       Path = $ShellSource },
    @{ Name = 'app.js';      Path = $AppJsSource },
    @{ Name = 'styles.css';  Path = $StylesCssSource }
)
foreach ($item in $required) {
    if (-not (Test-Path $item.Path)) {
        throw "Missing build artifact for $($item.Name): $($item.Path). Build the debug preset first: cmake --build --preset debug."
    }
}
if (-not (Test-Path $LivePath)) {
    throw "Live install path not found: $LivePath. Run MasterControlOrchestrationServerSetup.exe at least once to create it."
}

Write-Host "Stopping service $ServiceName..." -ForegroundColor Cyan
try { Stop-Service $ServiceName -Force -ErrorAction Stop } catch {
    Write-Warning "Service stop failed (may already be stopped): $($_.Exception.Message)"
}

# Stop any shell processes that are holding MasterControlShell.exe / .pdb open.
Get-Process MasterControlShell -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Host "Stopping shell PID $($_.Id)..." -ForegroundColor Cyan
    try { Stop-Process -Id $_.Id -Force } catch { }
}
Start-Sleep -Seconds 1

Write-Host "Copying build artifacts into $LivePath..." -ForegroundColor Cyan
Copy-Item $ServiceHostSource (Join-Path $LivePath 'MasterControlServiceHost.exe') -Force
Copy-Item $ShellSource       (Join-Path $LivePath 'MasterControlShell.exe')       -Force
if (Test-Path $ShellPdbSource) {
    Copy-Item $ShellPdbSource (Join-Path $LivePath 'MasterControlShell.pdb') -Force
}

# v0.9.98: copy all compiled XAML resources (.xbf), the merged resource
# index (.pri), and the WinRT metadata (.winmd) alongside the shell exe.
# Pre-v0.9.98 this script only copied the .exe + web assets, so every
# XAML / WinRT change shipped between v0.9.76 and v0.9.97 was invisible
# on the operator's live install -- the shell was loading stale .xbf
# files from the WiX MSI's original install. Root cause of the
# operator's "MCP server cards are missing from telemetry view" report:
# the v0.9.76 TelemetrySectionControl.xaml that added
# TelemetryMcpServersCardStack was never deployed; the shell's
# FindName('TelemetryMcpServersCardStack') silently failed against the
# stale 5/8 .xbf, leaving the C++ PopulateMcpServerCards call a no-op.
$ShellBuildDir = Join-Path $BuildDir 'src\MasterControlShell\Debug'
foreach ($pattern in '*.xbf', '*.pri', '*.winmd') {
    Get-ChildItem $ShellBuildDir -Filter $pattern -ErrorAction SilentlyContinue | ForEach-Object {
        Copy-Item $_.FullName (Join-Path $LivePath $_.Name) -Force
        Write-Host ("  + " + $_.Name) -ForegroundColor DarkGray
    }
}

Copy-Item $AppJsSource     (Join-Path $LiveWebRoot 'app.js')     -Force
Copy-Item $StylesCssSource (Join-Path $LiveWebRoot 'styles.css') -Force
if (Test-Path $IndexHtmlSource) {
    Copy-Item $IndexHtmlSource (Join-Path $LiveWebRoot 'index.html') -Force
}

Write-Host "Starting service $ServiceName..." -ForegroundColor Cyan
Start-Service $ServiceName

# Wait for the admin port to answer. The boot self-test sweep typically
# finishes within 3-5s of service start; give it a generous window before
# we declare a deploy failure.
$timeoutSeconds = 30
$elapsed = 0
$ok = $false
$lastError = ''
while ($elapsed -lt $timeoutSeconds) {
    try {
        $r = Invoke-WebRequest 'http://127.0.0.1:7300/api/version' -UseBasicParsing -TimeoutSec 3
        $version = ($r.Content | ConvertFrom-Json).version
        Write-Host "Service responded: version=$version" -ForegroundColor Green
        $ok = $true
        break
    } catch {
        $lastError = $_.Exception.Message
        Start-Sleep -Seconds 1
        $elapsed += 1
    }
}
if (-not $ok) {
    throw "Service did not respond within ${timeoutSeconds}s. Last error: $lastError"
}

# Self-tests sanity probe.
try {
    $st = (Invoke-WebRequest 'http://127.0.0.1:7300/api/self-tests' -UseBasicParsing -TimeoutSec 5).Content | ConvertFrom-Json
    Write-Host ("Self-tests: {0}/{1} passed (failed={2}, pending={3})" -f `
        $st.passedCount, $st.totalCount, $st.failedCount, $st.pending) -ForegroundColor Green
} catch {
    Write-Warning "Self-tests probe failed (service might still be initializing): $($_.Exception.Message)"
}

# Supervisor surface sanity probe.
try {
    $sup = (Invoke-WebRequest 'http://127.0.0.1:7300/api/supervisor/status' -UseBasicParsing -TimeoutSec 5).Content | ConvertFrom-Json
    Write-Host ("Supervisor: state={0} provider={1}" -f $sup.state, $sup.activeProviderId) -ForegroundColor Green
} catch {
    Write-Warning "Supervisor status probe failed: $($_.Exception.Message)"
}

if ($RelaunchShell) {
    Write-Host "Relaunching MasterControlShell..." -ForegroundColor Cyan
    $p = Start-Process (Join-Path $LivePath 'MasterControlShell.exe') -PassThru
    Start-Sleep -Seconds 4
    if ($p.HasExited) {
        Write-Warning "Shell exited with code $($p.ExitCode); check Windows Application event log."
    } else {
        Write-Host "Shell running as PID $($p.Id)." -ForegroundColor Green
    }
}

Write-Host "Deploy complete." -ForegroundColor Green
