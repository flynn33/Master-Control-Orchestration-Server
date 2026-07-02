<#
.SYNOPSIS
  Runs MCOS repository-health gates.
#>
[CmdletBinding()]
param(
  [string]$RepoRoot = (Get-Location).Path,
  [switch]$SkipBuild,
  [string]$LogDirectory = (Join-Path (Get-Location).Path (Join-Path 'artifacts' 'repository-health'))
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $RepoRoot).Path
New-Item -ItemType Directory -Force -Path $LogDirectory | Out-Null
$failures = 0
function Invoke-Gate([string]$Name, [scriptblock]$Body) {
  Write-Host "== $Name =="
  & $Body
  if ($LASTEXITCODE -ne 0) {
    Write-Host "$Name failed with exit code $LASTEXITCODE" -ForegroundColor Red
    $script:failures++
  }
}
$scriptsDir = Join-Path $root 'scripts'
Push-Location $root
try {
  Invoke-Gate 'JSON corpus' { pwsh -NoProfile -ExecutionPolicy Bypass -File (Join-Path $scriptsDir 'Test-MCOSJsonCorpus.ps1') -RepoRoot $root }
  Invoke-Gate 'Markdown links' { pwsh -NoProfile -ExecutionPolicy Bypass -File (Join-Path $scriptsDir 'Test-MCOSMarkdownLinks.ps1') -RepoRoot $root }
  Invoke-Gate 'Repository metadata' { pwsh -NoProfile -ExecutionPolicy Bypass -File (Join-Path $scriptsDir 'Test-MCOSRepositoryMetadata.ps1') -RepoRoot $root }
  if (Test-Path -LiteralPath (Join-Path $scriptsDir 'Sync-RepositoryVersionBadges.ps1')) {
    Invoke-Gate 'Version sync' { pwsh -NoProfile -ExecutionPolicy Bypass -File (Join-Path $scriptsDir 'Sync-RepositoryVersionBadges.ps1') -CheckOnly }
  }
  if (Test-Path -LiteralPath (Join-Path $scriptsDir 'Test-MCOSSecurityDefaults.ps1')) {
    Invoke-Gate 'Security defaults' { pwsh -NoProfile -ExecutionPolicy Bypass -File (Join-Path $scriptsDir 'Test-MCOSSecurityDefaults.ps1') -RepoRoot $root -LogDirectory $LogDirectory }
  }
  if (Test-Path -LiteralPath (Join-Path $scriptsDir 'Test-MCOSStaticGates.ps1')) {
    Invoke-Gate 'Static gates' { pwsh -NoProfile -ExecutionPolicy Bypass -File (Join-Path $scriptsDir 'Test-MCOSStaticGates.ps1') -RepoRoot $root -LogDirectory $LogDirectory }
  }
  if (-not $SkipBuild) {
    Invoke-Gate 'CMake configure debug' { cmake --preset debug }
    Invoke-Gate 'CMake build debug' { cmake --build --preset debug }
    Invoke-Gate 'CTest debug' { ctest --preset debug --output-on-failure }
  } else {
    Write-Warning 'SkipBuild specified; build and test gates were not run.'
  }
} finally {
  Pop-Location
}
if ($failures -gt 0) { exit 1 }
Write-Host 'Repository-health gates passed.'
exit 0
