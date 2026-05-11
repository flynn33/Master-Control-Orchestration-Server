# MCOS pool registration script (v0.9.4)
# Registers the four standard stdio MCP servers as MCOS-supervised pools so
# LAN AI clients reach them through the gateway at :8080/mcp instead of
# spawning their own per-client copies. This is Fix #6 from the v0.9.3
# deferred list ("Operator-bound external workers for the 23 named pools
# land in v0.9.x+").
#
# **Important deployment detail (v0.9.4):** we do NOT use `npx` as the
# pool executable. `npx` is a shim that spawns the actual MCP server as
# a CHILD of itself, so MCOS's stdio bridge ends up wired to the npx
# parent (which exits) rather than the actual server. The child server
# becomes an orphaned grandchild whose pid the supervisor never learns
# and whose stdio MCOS never connects to. Symptom: pool reaches Ready,
# but `tools/list` aggregation silently drops the pool because
# `sendStdioJsonRpc` reads back nothing.
#
# Fix: `npm install -g` the four packages, then spawn `node.exe`
# directly against the resolved `dist/index.js` for each. The MCP
# server runs as a direct child of MCOS under the Job Object, stdio
# is wired straight to MCOS, the supervisor's `mcpInitialized`
# handshake completes, and tools/list aggregates correctly.
#
# Idempotent: POST /api/pools is an upsert. Run any time to refresh
# definitions. Pool ids match the names used in Claude Code's .mcp.json
# so dashboards line up.

$ErrorActionPreference = "Stop"
$projectRoot = "G:\Claude\Master-Control-Orchastration-Server\master-control-dashboard-main"
$nodeExe     = "C:\Program Files\nodejs\node.exe"
$globalRoot  = "C:\Users\Flynn\AppData\Roaming\npm\node_modules"
$nodePath    = "C:\Program Files\nodejs\;C:\Windows\System32;C:\Windows"

function Make-Pool {
    param(
        [string]$poolId,
        [string]$displayName,
        [string]$entryScript,
        [string[]]$entryArgs = @(),
        [hashtable]$extraEnv = @{}
    )
    $args = @($entryScript) + $entryArgs
    $env  = @{ PATH = $nodePath } + $extraEnv
    return @{
        poolId         = $poolId
        displayName    = $displayName
        kind           = "mcp-server"
        logicalMcpUrl  = ""
        template       = @{
            executable       = $nodeExe
            args             = $args
            workingDirectory = $projectRoot
            environment      = $env
            transport        = "stdio_jsonrpc"
            healthProbe      = @{
                transport          = "stdio_handshake"
                intervalMs         = 5000
                timeoutMs          = 1500
                unhealthyThreshold = 3
                path               = ""
            }
        }
        scalePolicy    = @{
            minInstances              = 1
            maxInstances              = 2
            maxActiveLeasesPerInstance = 64
            scaleOutQueueWaitMs       = 1500
            scaleInIdleSeconds        = 300
        }
        drainPolicy    = @{
            drainTimeoutSeconds          = 30
            drainStickySessions          = $true
            routeNewSessionsToReplacement = $true
        }
    }
}

# Resolve each package's dist/index.js absolute path.
function Resolve-Entry($pkg) {
    $entry = Join-Path $globalRoot "$pkg\dist\index.js"
    if (-not (Test-Path $entry)) {
        throw "MCP entry not found at $entry. Run npm install -g $pkg first."
    }
    return $entry
}

$pools = @(
    Make-Pool -poolId "memory" `
              -displayName "Memory (knowledge graph) MCP" `
              -entryScript (Resolve-Entry "@modelcontextprotocol\server-memory") `
              -extraEnv @{ MEMORY_FILE_PATH = "$projectRoot\.claude\mcp-state\memory.jsonl" }

    Make-Pool -poolId "sequential-thinking" `
              -displayName "Sequential Thinking MCP" `
              -entryScript (Resolve-Entry "@modelcontextprotocol\server-sequential-thinking")

    Make-Pool -poolId "filesystem" `
              -displayName "Filesystem MCP (rooted at MCOS project)" `
              -entryScript (Resolve-Entry "@modelcontextprotocol\server-filesystem") `
              -entryArgs @($projectRoot)

    Make-Pool -poolId "sqlite" `
              -displayName "SQLite MCP" `
              -entryScript (Resolve-Entry "mcp-server-sqlite-npx") `
              -entryArgs @("$projectRoot\.claude\mcp-state\mcos-orchestration.sqlite")
)

# Drop any previous registrations first so the supervisor reaps the npx-
# parent zombies from the v0.9.4 first attempt.
Write-Host "==> Removing prior pool registrations..." -ForegroundColor Yellow
foreach ($p in $pools) {
    try {
        $r = Invoke-RestMethod -Uri "http://localhost:7300/api/pools/$($p.poolId)/remove" -Method POST -Body "{}" -ContentType "application/json" -TimeoutSec 30
        "  removed $($p.poolId): $($r.message)"
    } catch {
        "  remove $($p.poolId): not present or already gone"
    }
}

Start-Sleep -Seconds 1

Write-Host ""
Write-Host "==> Registering pools (direct node.exe entry-point spawn)..." -ForegroundColor Cyan
foreach ($pool in $pools) {
    $body = $pool | ConvertTo-Json -Depth 10
    Write-Host "  $($pool.poolId)"
    try {
        $resp = Invoke-RestMethod -Uri "http://localhost:7300/api/pools" -Method POST -Body $body -ContentType "application/json" -TimeoutSec 30
        "    succeeded=$($resp.succeeded) message='$($resp.message)'"
    } catch {
        "    POST failed: $_"
    }
}

Write-Host ""
Write-Host "==> Scaling each pool to minInstances=1..." -ForegroundColor Cyan
foreach ($pool in $pools) {
    try {
        $r = Invoke-RestMethod -Uri "http://localhost:7300/api/pools/$($pool.poolId)/scale" -Method POST -Body "{}" -ContentType "application/json" -TimeoutSec 30
        "  $($pool.poolId): $($r.message)"
    } catch {
        "  $($pool.poolId): scale failed -- $_"
    }
}

Write-Host ""
Write-Host "Waiting 6s for instances to come up..." -ForegroundColor Yellow
Start-Sleep -Seconds 6

$allPools = Invoke-RestMethod -Uri "http://localhost:7300/api/pools" -TimeoutSec 30
Write-Host ""
Write-Host "=== Final pool roster ===" -ForegroundColor Green
foreach ($p in $allPools) {
    $instStates = ($p.instances | ForEach-Object { "$($_.instanceId)=$($_.state) (pid=$($_.processId))" }) -join ', '
    "  $($p.poolId) [$($p.kind)]: $instStates"
}
