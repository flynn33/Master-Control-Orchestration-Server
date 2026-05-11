# MCOS Sub-Agent Pool Attacher
#
# Sub-agents are spawned by Claude Code itself (the Agent tool) so we cannot
# wrap them at launch the way we do MCP servers. Instead this script scans
# Claude Code's process tree for sub-agent processes and assigns each one to
# the sub-agents pool.
#
# Heuristics for "is this a sub-agent process":
#   - Direct child of a `claude.exe` (or `claude.cmd`/`node.exe` running
#     Claude Code) process AND
#   - Image name suggests a sub-agent worker (claude.exe, node.exe under
#     the Claude Code install dir) AND
#   - Not already in the sub-agents pool
#
# This is conservative — false negatives mean an unenforced sub-agent (visible
# via pool-status); false positives could trap unrelated child processes, so we
# require the parent-image match.
#
# Modes:
#   -Once  : single sweep (default)
#   -Watch : sweep every <Interval> seconds until killed (good for background)

[CmdletBinding()]
param(
    [string]$PoolKey = "sub-agents",
    [switch]$Watch,
    [int]$IntervalSeconds = 5
)

$ErrorActionPreference = "Stop"

Import-Module (Join-Path $PSScriptRoot "pool-governor.psm1") -Force
$policy = Get-McosPoolPolicy

# Make sure the pool exists before we try to attach to it.
$null = Initialize-McosResourcePool -PoolKey $PoolKey -PolicyObject $policy

function Find-ClaudeRoots {
    # Returns pids of likely Claude Code "session" processes (the ones that
    # spawn sub-agents). We accept any process whose image name contains
    # 'claude' OR is node.exe with a 'claude' substring in its command line.
    $roots = @()
    Get-CimInstance Win32_Process | ForEach-Object {
        $imageLower = ($_.Name).ToLowerInvariant()
        $cmdLower   = if ($_.CommandLine) { ($_.CommandLine).ToLowerInvariant() } else { "" }
        if ($imageLower -like "*claude*") { $roots += $_.ProcessId; return }
        if ($imageLower -eq "node.exe" -and $cmdLower -like "*claude*") { $roots += $_.ProcessId; return }
    }
    return ($roots | Sort-Object -Unique)
}

function Get-DescendantPids([int[]]$rootPids) {
    if (-not $rootPids -or $rootPids.Count -eq 0) { return @() }
    $procs = Get-CimInstance Win32_Process
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
        if ($byParent.ContainsKey($cur)) {
            foreach ($child in $byParent[$cur]) { $stack.Push([int]$child) }
        }
    }
    return ($seen.Keys | Where-Object { $rootPids -notcontains $_ })
}

function Sweep-Once {
    $roots = Find-ClaudeRoots
    if (-not $roots -or $roots.Count -eq 0) {
        Write-Host "pool-attach-subagents: no Claude Code root processes found"
        return
    }
    $descendants = Get-DescendantPids $roots
    Write-Host ("pool-attach-subagents: roots={0} descendants={1}" -f ($roots -join ","), $descendants.Count)

    $attached = 0; $skipped = 0; $failed = 0
    foreach ($pid in $descendants) {
        try {
            $r = Add-ProcessToMcosResourcePool -PoolKey $PoolKey -PolicyObject $policy -ProcessId $pid
            if ($r.Assigned) { $attached++; Write-Host ("  + pid={0} attached" -f $pid) }
            else             { $skipped++  }
        } catch {
            $failed++
            Write-Warning ("  - pid={0} attach failed: {1}" -f $pid, $_.Exception.Message)
        }
    }
    Write-Host ("pool-attach-subagents: attached={0} skipped={1} failed={2}" -f $attached, $skipped, $failed)
}

if ($Watch) {
    Write-Host ("pool-attach-subagents: watch mode, interval={0}s" -f $IntervalSeconds)
    while ($true) {
        try { Sweep-Once } catch { Write-Warning $_ }
        Start-Sleep -Seconds $IntervalSeconds
    }
} else {
    Sweep-Once
}
