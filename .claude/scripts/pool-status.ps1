# MCOS Resource Pool Status
#
# Reports the real, kernel-reported state of each pool. No fake metrics.

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

Import-Module (Join-Path $PSScriptRoot "pool-governor.psm1") -Force
$policy = Get-McosPoolPolicy

Write-Host "=== MCOS Resource Pool Status ===" -ForegroundColor Green
Write-Host ("Policy: {0}, authorized {1}" -f $policy.version, $policy.authorizedAt) -ForegroundColor DarkGray
Write-Host ""

foreach ($key in $policy.pools.PSObject.Properties.Name) {
    $status = Get-McosResourcePoolStatus -PoolKey $key -PolicyObject $policy
    Write-Host ("[{0}]" -f $key) -ForegroundColor Cyan
    if (-not $status.Exists) {
        Write-Host ("  NOT INITIALIZED ({0})" -f $status.Note) -ForegroundColor Yellow
        continue
    }
    $userSec   = [math]::Round($status.TotalUserTime100ns   / 10000000.0, 2)
    $kernelSec = [math]::Round($status.TotalKernelTime100ns / 10000000.0, 2)
    $peakMB    = [math]::Round($status.PeakJobMemoryBytes  / 1MB, 1)
    $limitGB   = [math]::Round($status.JobMemoryLimitBytes / 1GB, 2)
    Write-Host ("  job:       {0}" -f $status.JobObjectName)
    Write-Host ("  active:    {0} processes" -f $status.ActiveProcesses)
    Write-Host ("  ever:      {0} processes (cumulative)" -f $status.TotalProcessesEverInJob)
    Write-Host ("  CPU time:  user={0}s kernel={1}s" -f $userSec, $kernelSec)
    Write-Host ("  memory:    peak={0} MB / limit={1} GB" -f $peakMB, $limitGB)
    Write-Host ("  authority: up to {0}% CPU / {1}% memory" -f $status.AuthorizedCpuPercent, $status.AuthorizedMemoryPercent) -ForegroundColor DarkGray
}
