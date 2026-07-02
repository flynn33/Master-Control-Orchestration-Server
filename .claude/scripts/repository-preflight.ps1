# Master Control Orchestration Server - session preflight gate
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.
#
# Enforces the repository invariants a session depends on before any work
# happens: the governing documents exist, the version authority and program
# manifest parse, and the protected attribution-guard paths are present.
# Exit 2 (blocking) when an invariant is broken; otherwise prints the
# required-reading list as session context and exits 0.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot (Join-Path '..' '..'))
$violations = @()

$requiredDocs = @('CLAUDE.md', 'AGENTS.md', 'VERSION.json', (Join-Path 'handoff' (Join-Path 'realignment' 'manifest.json')))
foreach ($doc in $requiredDocs) {
    if (-not (Test-Path -LiteralPath (Join-Path $repoRoot $doc))) {
        $violations += "Required project file missing: $doc"
    }
}

foreach ($jsonDoc in @('VERSION.json', (Join-Path 'handoff' (Join-Path 'realignment' 'manifest.json')))) {
    $path = Join-Path $repoRoot $jsonDoc
    if (Test-Path -LiteralPath $path) {
        try { $null = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json -ErrorAction Stop }
        catch { $violations += "Cannot parse ${jsonDoc}: $($_.Exception.Message)" }
    }
}

$protectedPaths = @(
    (Join-Path '.github' (Join-Path 'workflows' 'ai-contributor-guard.yml')),
    (Join-Path '.github' 'copilot-instructions.md'),
    (Join-Path 'scripts' (Join-Path 'github_agents' 'check_no_ai_contributors.py')),
    (Join-Path 'scripts' (Join-Path 'github_agents' 'common.py')),
    (Join-Path 'scripts' 'Invoke-AiContributorGuard.ps1')
)
foreach ($protected in $protectedPaths) {
    if (-not (Test-Path -LiteralPath (Join-Path $repoRoot $protected))) {
        $violations += "Protected attribution-guard path missing: $protected"
    }
}

if ($violations.Count -gt 0) {
    [Console]::Error.WriteLine("MCOS preflight gate FAILED:`n - " + ($violations -join "`n - "))
    exit 2
}

Write-Host 'MCOS preflight: read CLAUDE.md, AGENTS.md, VERSION.json, and handoff/realignment/manifest.json before editing. Protected attribution-guard paths are read-only. Source edits require a scope plan at .claude/state/scope-plan.md.'
exit 0
