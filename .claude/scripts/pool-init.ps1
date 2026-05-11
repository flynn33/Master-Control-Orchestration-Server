# MCOS Resource Pool Initialization
#
# Idempotent. Run from the SessionStart hook.
#
# 1. Create both named Job Objects (mcp-servers + sub-agents) with policy
#    limits (90% CPU HARD_CAP, 90% physical memory, 16-core affinity).
# 2. Spawn a background anchor for each so the kernel objects persist even
#    when no workers are currently inside.
# 3. Print a short authorization summary so the operator (and anyone reading
#    the session log) sees what was set.

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$projectRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")

Import-Module (Join-Path $PSScriptRoot "pool-governor.psm1") -Force
$policy = Get-McosPoolPolicy

$created = @()
foreach ($key in $policy.pools.PSObject.Properties.Name) {
    $pool = $policy.pools.$key
    Write-Host ("==> Initializing pool '{0}' ({1})" -f $key, $pool.jobObjectName) -ForegroundColor Cyan
    $init = Initialize-McosResourcePool -PoolKey $key -PolicyObject $policy
    Write-Host ("    cpuRate={0}% (hardCap={1}), memCap={2:N0} bytes (~{3} GB), affinity={4}" `
                -f ($init.CpuRateHundredths/100), $init.CpuHardCap, $init.JobMemoryBytes, [math]::Round($init.JobMemoryBytes/1GB,2), $init.AffinityMaskHex) -ForegroundColor DarkGray

    # Anchor: skip if pid file says one is already alive.
    $pidFile = Join-Path $PSScriptRoot ("..\mcp-state\pool-anchor-{0}.pid" -f $key)
    $needAnchor = $true
    if (Test-Path $pidFile) {
        $existingPid = (Get-Content $pidFile -ErrorAction SilentlyContinue) -as [int]
        if ($existingPid -and (Get-Process -Id $existingPid -ErrorAction SilentlyContinue)) {
            Write-Host ("    anchor pid={0} already alive, skipping spawn" -f $existingPid) -ForegroundColor DarkGray
            $needAnchor = $false
        }
    }
    if ($needAnchor) {
        $anchorScript = Join-Path $PSScriptRoot "pool-anchor.ps1"
        $proc = Start-Process -FilePath "powershell.exe" `
                              -ArgumentList @("-NoProfile","-ExecutionPolicy","Bypass","-File",$anchorScript,"-PoolKey",$key) `
                              -WindowStyle Hidden -PassThru
        Write-Host ("    spawned anchor pid={0}" -f $proc.Id) -ForegroundColor DarkGray
    }
    $created += $key
}

# Sweeper: skip if pid file says one is already alive.
$sweeperPidFile = Join-Path $PSScriptRoot "..\mcp-state\pool-sweeper.pid"
$needSweeper = $true
if (Test-Path $sweeperPidFile) {
    $existingSweeperPid = (Get-Content $sweeperPidFile -ErrorAction SilentlyContinue) -as [int]
    if ($existingSweeperPid -and (Get-Process -Id $existingSweeperPid -ErrorAction SilentlyContinue)) {
        Write-Host ("==> sweeper pid={0} already alive, skipping spawn" -f $existingSweeperPid) -ForegroundColor DarkGray
        $needSweeper = $false
    }
}
if ($needSweeper) {
    $sweeperScript = Join-Path $PSScriptRoot "pool-sweeper.ps1"
    $sweeperProc = Start-Process -FilePath "powershell.exe" `
                                 -ArgumentList @("-NoProfile","-ExecutionPolicy","Bypass","-File",$sweeperScript) `
                                 -WindowStyle Hidden -PassThru
    Write-Host ("==> spawned sweeper pid={0}" -f $sweeperProc.Id) -ForegroundColor Cyan
}

Write-Host ""
Write-Host "=== MCOS Resource Pool Authorization Summary ===" -ForegroundColor Green
foreach ($key in $created) {
    $pool = $policy.pools.$key
    Write-Host ("  {0}: authorized up to {1}% CPU + {2}% memory ({3} GB) via {4}" `
                -f $pool.displayName, $pool.authorizedShare.cpuPercent, $pool.authorizedShare.memoryPercent, $pool.authorizedShare.memoryGB, $pool.jobObjectName)
}
Write-Host "  (Caps are hard-enforced via Windows Job Objects; sweeper attaches new MCP/sub-agent processes within ~3s.)" -ForegroundColor DarkGray
