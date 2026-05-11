# MCOS Resource Pool Sweeper
#
# Long-running background process. Walks the Claude Code process tree on a
# fixed interval and attaches each descendant to the correct pool:
#
#   - claude.exe descendants   -> sub-agents pool
#   - node.exe / py.exe / python.exe / powershell.exe / cmd.exe descendants
#     of a Claude Code root    -> mcp-servers pool
#
# Why a sweeper rather than launch-time wrappers?
#   - MCP servers in .mcp.json launch via diverse runtimes (node, py, powershell).
#     Wrapping each entry inflates .mcp.json substantially and any typo bricks
#     the MCP transport. A sweeper keeps .mcp.json untouched.
#   - Sub-agents are spawned by Claude Code internally (Agent tool); there is
#     no launch hook to wrap.
#   - The window between "process spawned" and "first sweep" is bounded by
#     IntervalSeconds (default 3s). For a 90% cap that brief window cannot
#     do damage.
#
# Logs to .claude/mcp-state/pool-sweeper.log (rotated when >1 MB).

[CmdletBinding()]
param(
    [int]$IntervalSeconds = 3
)

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "pool-governor.psm1") -Force
$policy = Get-McosPoolPolicy

$logFile = Join-Path $PSScriptRoot "..\mcp-state\pool-sweeper.log"
$pidFile = Join-Path $PSScriptRoot "..\mcp-state\pool-sweeper.pid"
$logDir  = Split-Path -Parent $logFile
if (-not (Test-Path $logDir)) { New-Item -ItemType Directory -Path $logDir | Out-Null }

function Write-SweeperLog([string]$msg) {
    if ((Test-Path $logFile) -and ((Get-Item $logFile).Length -gt 1MB)) {
        Move-Item $logFile "$logFile.1" -Force
    }
    "{0:o}  {1}" -f (Get-Date), $msg | Add-Content -Encoding ascii -Path $logFile
}

"$PID" | Set-Content -Encoding ascii -Path $pidFile
Write-SweeperLog "sweeper pid=$PID started, interval=${IntervalSeconds}s"

# Bring both pools up so OpenJobObjectW from the sweeper succeeds even if
# pool-init.ps1 hasn't finished spawning anchors yet.
foreach ($key in $policy.pools.PSObject.Properties.Name) {
    try { $null = Initialize-McosResourcePool -PoolKey $key -PolicyObject $policy } catch {
        Write-SweeperLog "Initialize-McosResourcePool($key) FAILED: $_"
    }
}

$attached = @{} # pid -> pool name (avoids re-attaching every sweep)

# Image-name classifier
$mcpServerImages = @("node.exe","py.exe","python.exe","python3.exe","powershell.exe","pwsh.exe","cmd.exe")
$subAgentImages  = @("claude.exe")

function Find-ClaudeRoots {
    $roots = @()
    Get-CimInstance Win32_Process | ForEach-Object {
        $imageLower = if ($_.Name) { $_.Name.ToLowerInvariant() } else { "" }
        if ($imageLower -like "*claude*") { $roots += [int]$_.ProcessId; return }
    }
    return ($roots | Sort-Object -Unique)
}

function Get-Descendants([int[]]$rootPids, $procs) {
    if (-not $rootPids -or $rootPids.Count -eq 0) { return @() }
    $byParent = @{}
    foreach ($p in $procs) {
        $key = [int]$p.ParentProcessId
        if (-not $byParent.ContainsKey($key)) { $byParent[$key] = @() }
        $byParent[$key] += [int]$p.ProcessId
    }
    $seen = @{}
    $stack = New-Object System.Collections.Stack
    foreach ($r in $rootPids) { $stack.Push($r) }
    while ($stack.Count -gt 0) {
        $cur = [int]$stack.Pop()
        if ($seen.ContainsKey($cur)) { continue }
        $seen[$cur] = $true
        if ($byParent.ContainsKey($cur)) { foreach ($c in $byParent[$cur]) { $stack.Push([int]$c) } }
    }
    return ($seen.Keys | Where-Object { $rootPids -notcontains $_ })
}

function Choose-PoolForImage([string]$imageNameLower) {
    if ($subAgentImages -contains $imageNameLower) { return "sub-agents" }
    if ($mcpServerImages -contains $imageNameLower) { return "mcp-servers" }
    return $null   # unrecognized -> leave alone
}

while ($true) {
    try {
        $procs = Get-CimInstance Win32_Process
        $roots = Find-ClaudeRoots
        if ($roots.Count -eq 0) {
            # No Claude Code running; idle.
            Start-Sleep -Seconds $IntervalSeconds
            continue
        }
        $descendants = Get-Descendants $roots $procs
        $procById = @{}
        foreach ($p in $procs) { $procById[[int]$p.ProcessId] = $p }

        $newAttachments = 0
        foreach ($d in $descendants) {
            if ($attached.ContainsKey($d)) { continue }   # already done
            $proc = $procById[[int]$d]
            if (-not $proc) { continue }
            $img = if ($proc.Name) { $proc.Name.ToLowerInvariant() } else { "" }
            $pool = Choose-PoolForImage $img
            if (-not $pool) { continue }
            try {
                $r = Add-ProcessToMcosResourcePool -PoolKey $pool -PolicyObject $policy -ProcessId $d
                if ($r.Assigned) {
                    $attached[$d] = $pool
                    $newAttachments++
                    Write-SweeperLog ("ATTACH pid={0} image='{1}' -> {2}" -f $d, $img, $pool)
                } elseif ($r.Reason -eq "already in this pool") {
                    $attached[$d] = $pool   # remember so we stop checking
                } else {
                    Write-SweeperLog ("SKIP   pid={0} image='{1}' -> {2}: {3}" -f $d, $img, $pool, $r.Reason)
                    $attached[$d] = "skipped"
                }
            } catch {
                Write-SweeperLog ("ERROR  pid={0} image='{1}' -> {2}: {3}" -f $d, $img, $pool, $_.Exception.Message)
            }
        }

        # Garbage-collect dead pids from the cache so the table doesn't grow forever.
        $deadPids = $attached.Keys | Where-Object { -not $procById.ContainsKey([int]$_) }
        foreach ($d in $deadPids) { $attached.Remove($d) | Out-Null }

        if ($newAttachments -gt 0) {
            Write-SweeperLog ("sweep: roots={0} descendants={1} new_attachments={2} tracked={3}" -f $roots.Count, $descendants.Count, $newAttachments, $attached.Count)
        }
    } catch {
        Write-SweeperLog "SWEEP THREW: $_"
    }
    Start-Sleep -Seconds $IntervalSeconds
}
