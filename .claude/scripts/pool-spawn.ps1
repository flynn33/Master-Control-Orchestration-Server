# MCOS Pool Spawn Wrapper
#
# Used by .mcp.json so every MCP server launch lands inside a named pool.
#
# Behavior:
#   1. Open or create the named pool with policy limits.
#   2. Assign the wrapper itself to the pool. Children spawned via the `&`
#      operator inherit the job by default (BREAKAWAY_OK is not set).
#   3. Exec the actual MCP server command, passing stdio straight through
#      so JSON-RPC over stdio works unchanged.
#
# Honesty rules (per .claude/rules + CLAUDE.md):
#   - If the pool can't be applied (e.g., outer Claude Code job blocks
#     nested HARD_CAP), the wrapper logs WARNING to stderr and STILL
#     execs the command. The MCP server starts unenforced rather than
#     failing the session. The unenforced state is visible via
#     pool-status.ps1 / Get-McosResourcePoolStatus.
#   - The wrapper does NOT pretend the pool is applied when it isn't.
#
# Usage:
#   pool-spawn.ps1 -PoolKey mcp-servers <executable> [<args>...]
#
# All tokens after -PoolKey value are captured as the inner command line
# (NO `--` separator — PowerShell parses `--` as an empty parameter name).
#
# Example .mcp.json entry:
#   "filesystem": {
#     "command": "powershell.exe",
#     "args": [
#       "-NoProfile","-ExecutionPolicy","Bypass",
#       "-File",".claude/scripts/pool-spawn.ps1",
#       "-PoolKey","mcp-servers",
#       "C:\\Program Files\\nodejs\\node.exe", "...", ...
#     ]
#   }

[CmdletBinding(PositionalBinding=$false)]
param(
    [Parameter(Mandatory)] [string]$PoolKey,
    [Parameter(ValueFromRemainingArguments=$true)] [string[]]$Remaining
)

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "pool-governor.psm1") -Force

if (-not $Remaining -or $Remaining.Count -eq 0) {
    [Console]::Error.WriteLine("pool-spawn[$PoolKey]: no command supplied after -PoolKey")
    exit 64
}

$exe  = [string]$Remaining[0]
$rest = if ($Remaining.Count -gt 1) { $Remaining[1..($Remaining.Count-1)] | ForEach-Object { [string]$_ } } else { @() }

# Apply pool to self. Children inherit the job. We use Connect-McosResourcePool
# so the job handle is held for the wrapper's lifetime (== child's lifetime).
try {
    $script:conn = Connect-McosResourcePool -PoolKey $PoolKey
    if (-not $script:conn.Assigned) {
        [Console]::Error.WriteLine(("pool-spawn[$PoolKey]: WARNING - wrapper not in pool ({0}); child '$exe' will run UNENFORCED" -f $script:conn.Reason))
    } else {
        [Console]::Error.WriteLine("pool-spawn[$PoolKey]: wrapper pid=$PID joined pool, child '$exe' inherits cap")
    }
} catch {
    [Console]::Error.WriteLine("pool-spawn[$PoolKey]: WARNING - pool connect failed: $_; child '$exe' will run UNENFORCED")
}

# Hand stdio through to the child. & writes the child's stdout straight back
# to the wrapper's stdout (which is Claude Code's pipe), so JSON-RPC works.
& $exe @rest
exit $LASTEXITCODE
