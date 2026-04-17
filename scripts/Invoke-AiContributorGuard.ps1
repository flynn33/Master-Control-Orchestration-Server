Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptRoot "..")
$inputPayload = [Console]::In.ReadToEnd()

if ([string]::IsNullOrWhiteSpace($inputPayload)) {
    exit 0
}

$inputPayload | & py -3 (Join-Path $repoRoot "scripts/github_agents/check_no_ai_contributors.py") --hook
exit $LASTEXITCODE
