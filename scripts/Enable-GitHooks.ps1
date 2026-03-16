$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptRoot "..")

git -C $repoRoot config core.hooksPath .githooks
Write-Host "Git hooks enabled for $repoRoot using .githooks"
