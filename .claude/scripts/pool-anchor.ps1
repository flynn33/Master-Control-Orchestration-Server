# MCOS Resource Pool Anchor
#
# A long-lived, near-zero-cost process that joins a named MCOS pool and holds
# it open for the entire Claude Code session. Without an anchor the pool's
# kernel object is reaped the moment all handles close and no member
# processes exist, which would defeat session-start initialization.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File pool-anchor.ps1 -PoolKey mcp-servers
#
# This script is normally launched in the background by pool-init.ps1 and
# survives until the user closes the session (or the anchor is killed). It
# logs its pid to .claude/mcp-state/pool-anchor-<PoolKey>.pid for cleanup.

[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$PoolKey
)

$ErrorActionPreference = "Stop"

Import-Module (Join-Path $PSScriptRoot "pool-governor.psm1") -Force

$policy = Get-McosPoolPolicy
$pool = $policy.pools.$PoolKey
if (-not $pool) {
    Write-Error "anchor: unknown pool '$PoolKey'"
    exit 2
}

# Diagnostics file (anchor runs hidden, otherwise output is lost).
$logFile = Join-Path $PSScriptRoot "..\mcp-state\pool-anchor-$PoolKey.log"
$logDir  = Split-Path -Parent $logFile
if (-not (Test-Path $logDir)) { New-Item -ItemType Directory -Path $logDir | Out-Null }
function Write-AnchorLog([string]$msg) {
    "{0:o}  {1}" -f (Get-Date), $msg | Add-Content -Encoding ascii -Path $logFile
}

Write-AnchorLog ("anchor pid=$PID starting for pool '$PoolKey'")

# Connect atomically (init + assign self with the handle kept open for the
# anchor's lifetime). $conn.Handle MUST stay in scope or the named kernel
# object loses its anchor handle the moment we close it.
try {
    $script:conn = Connect-McosResourcePool -PoolKey $PoolKey -PolicyObject $policy
    Write-AnchorLog ("Connect-McosResourcePool: Assigned={0} Reason='{1}' Handle=0x{2:X}" -f $conn.Assigned, $conn.Reason, [int64]$conn.Handle)
} catch {
    Write-AnchorLog "Connect-McosResourcePool THREW: $_"
    exit 3
}

# Persist pid (always, even if assign failed — operator needs to find this proc).
$pidFile = Join-Path $PSScriptRoot "..\mcp-state\pool-anchor-$PoolKey.pid"
"$PID" | Set-Content -Encoding ascii -Path $pidFile
Write-AnchorLog "wrote pid file $pidFile"

# Block forever; idle CPU ~0%. The held $script:conn.Handle keeps the named
# job object alive until this process exits.
while ($true) { Start-Sleep -Seconds 3600 }
