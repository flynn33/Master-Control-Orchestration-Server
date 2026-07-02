# Master Control Orchestration Server - pre-edit scope gate
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.
#
# PreToolUse hook for Write/Edit/MultiEdit. Reads the hook payload from
# stdin and enforces two rules:
#   1. The protected attribution-guard paths may never be edited (exit 2).
#   2. Product-source edits (src/, include/, tests/, installer/,
#      resources/) require a file-by-file scope plan at
#      .claude/state/scope-plan.md that names the target file (or contains
#      a literal "*" to approve a whole-tree operation). Matches the
#      manifest executionPolicy.requiresFileByFilePlanBeforeEdits rule.
# Exit 0 allows the tool call; exit 2 blocks it with the reason on stderr.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot (Join-Path '..' '..'))).Path

$filePath = $null
try {
    $payload = [Console]::In.ReadToEnd() | ConvertFrom-Json
    $filePath = [string]$payload.tool_input.file_path
} catch {
    # Malformed or empty payload: do not block, but say so.
    [Console]::Error.WriteLine("pre-edit-scope-gate: could not read a file_path from the hook payload; allowing call.")
    exit 0
}
if ([string]::IsNullOrWhiteSpace($filePath)) { exit 0 }

# Canonicalize before comparing so '..' and '.' segments cannot dodge the
# protected-path or scope checks, then reduce to a repo-relative path with
# forward slashes.
$candidate = $filePath -replace '\\', '/'
if (-not [System.IO.Path]::IsPathRooted($candidate)) {
    $candidate = Join-Path $repoRoot $candidate
}
$candidate = [System.IO.Path]::GetFullPath($candidate)
$normalized = $candidate -replace '\\', '/'
$rootNorm = ($repoRoot -replace '\\', '/').TrimEnd('/')
if ($normalized.StartsWith($rootNorm, [System.StringComparison]::OrdinalIgnoreCase)) {
    $normalized = $normalized.Substring($rootNorm.Length).TrimStart('/')
}

$protectedPaths = @(
    '.github/workflows/ai-contributor-guard.yml',
    '.github/copilot-instructions.md',
    'scripts/github_agents/check_no_ai_contributors.py',
    'scripts/github_agents/common.py',
    'scripts/Invoke-AiContributorGuard.ps1'
)
foreach ($protected in $protectedPaths) {
    if ($normalized -ieq $protected) {
        [Console]::Error.WriteLine("BLOCKED: $protected is a protected attribution-guard path and is read-only. If a task appears to require changing it, stop and report the conflict.")
        exit 2
    }
}

$sourcePrefixes = @('src/', 'include/', 'tests/', 'installer/', 'resources/')
$isSource = $false
foreach ($prefix in $sourcePrefixes) {
    if ($normalized.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) { $isSource = $true; break }
}
if (-not $isSource) { exit 0 }

$planPath = Join-Path $repoRoot (Join-Path '.claude' (Join-Path 'state' 'scope-plan.md'))
if (-not (Test-Path -LiteralPath $planPath)) {
    [Console]::Error.WriteLine("BLOCKED: source edit to '$normalized' without a scope plan. Write a file-by-file plan to .claude/state/scope-plan.md (list every file you will touch) before editing product source.")
    exit 2
}
$plan = Get-Content -LiteralPath $planPath -Raw
$leaf = Split-Path -Leaf $normalized
if (-not ($plan.Contains($leaf) -or $plan.Contains('*'))) {
    [Console]::Error.WriteLine("BLOCKED: '$leaf' is not listed in .claude/state/scope-plan.md. Add it to the scope plan (or justify a whole-tree operation with '*') before editing.")
    exit 2
}
exit 0
