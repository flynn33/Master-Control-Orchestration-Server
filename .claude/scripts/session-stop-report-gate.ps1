# Master Control Orchestration Server - session stop-report gate
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.
#
# Stop hook. A session may not end with uncommitted repository changes
# unless a stop report exists at .claude/state/stop-report.md containing
# the required sections (Files changed / Validation / Risks). Sessions
# that left the working tree clean pass without a report.
# Exit 0 allows the stop; exit 2 blocks it with the reason on stderr.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot (Join-Path '..' '..'))).Path

# Never loop: if this stop was already forced to continue by this hook,
# allow it through.
try {
    $payload = [Console]::In.ReadToEnd() | ConvertFrom-Json
    if ($payload.PSObject.Properties['stop_hook_active'] -and $payload.stop_hook_active) { exit 0 }
} catch { }

$dirty = ''
try {
    $dirty = (& git -C $repoRoot status --porcelain 2>$null | Out-String).Trim()
} catch { exit 0 }
if ([string]::IsNullOrWhiteSpace($dirty)) { exit 0 }

$reportPath = Join-Path $repoRoot (Join-Path '.claude' (Join-Path 'state' 'stop-report.md'))
if (-not (Test-Path -LiteralPath $reportPath)) {
    [Console]::Error.WriteLine('MCOS stop gate: the working tree has uncommitted changes and no stop report exists. Write .claude/state/stop-report.md with sections: ## Files changed, ## Validation, ## Risks (include gate/test results actually run), then stop again.')
    exit 2
}
$report = Get-Content -LiteralPath $reportPath -Raw
$missing = @()
foreach ($section in @('## Files changed', '## Validation', '## Risks')) {
    if (-not $report.Contains($section)) { $missing += $section }
}
if ($missing.Count -gt 0) {
    [Console]::Error.WriteLine("MCOS stop gate: .claude/state/stop-report.md is missing required section(s): $($missing -join ', '). Complete the report, then stop again.")
    exit 2
}
exit 0
