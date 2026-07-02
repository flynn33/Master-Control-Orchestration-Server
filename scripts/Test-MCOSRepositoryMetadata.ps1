<#
.SYNOPSIS
  Validates MCOS repository metadata consistency.
#>
[CmdletBinding()]
param(
  [string]$RepoRoot = (Get-Location).Path,
  [string[]]$ExcludeDirectory = @('.git','build','dist','out','artifacts','Testing','TestResults','__pycache__','node_modules')
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $RepoRoot).Path
function Test-ExcludedPath([string]$RelativePath) {
  $parts = $RelativePath -split '[\\/]'
  return [bool]($parts | Where-Object { $ExcludeDirectory -contains $_ })
}
$failures = New-Object System.Collections.Generic.List[string]
$version = $null
function Read-Text([string]$RelativePath) {
  $path = Join-Path $root $RelativePath
  if (-not (Test-Path -LiteralPath $path)) {
    $failures.Add("Missing required file: $RelativePath") | Out-Null
    return ''
  }
  return [System.IO.File]::ReadAllText($path)
}
function Add-Failure([string]$Message) { $failures.Add($Message) | Out-Null }
$versionPath = Join-Path $root 'VERSION.json'
if (-not (Test-Path -LiteralPath $versionPath)) { Add-Failure 'VERSION.json missing' }
else {
  try { $version = Get-Content -LiteralPath $versionPath -Raw | ConvertFrom-Json -ErrorAction Stop }
  catch { Add-Failure ("VERSION.json parse failure: $($_.Exception.Message)") }
}
if ($null -ne $version) {
  $current = [string]$version.current_version
  $tag = [string]$version.current_tag
  if ([string]::IsNullOrWhiteSpace($current)) { Add-Failure 'VERSION.json current_version missing' }
  if ($tag -ne "v$current") { Add-Failure "VERSION.json current_tag '$tag' does not match v$current" }
  if ($version.history.Count -lt 1) { Add-Failure 'VERSION.json history is empty' }
  else {
    $latest = $version.history[0]
    if ([string]$latest.version -ne $current) { Add-Failure "VERSION.json history[0].version '$($latest.version)' does not match current_version '$current'" }
    if ([string]$latest.tag -ne "v$current") { Add-Failure "VERSION.json history[0].tag '$($latest.tag)' does not match v$current" }
  }
  $vcpkgPath = Join-Path $root 'vcpkg.json'
  if (Test-Path -LiteralPath $vcpkgPath) {
    try {
      $vcpkg = Get-Content -LiteralPath $vcpkgPath -Raw | ConvertFrom-Json -ErrorAction Stop
      if ([string]$vcpkg.'version-string' -ne $current) { Add-Failure "vcpkg.json version-string '$($vcpkg.'version-string')' does not match '$current'" }
    } catch { Add-Failure ("vcpkg.json parse failure: $($_.Exception.Message)") }
  } else { Add-Failure 'vcpkg.json missing' }
  $currentNeedle = "v$current"
  foreach ($relative in @('README.md','docs/wiki/Home.md','docs/wiki/Versions.md','docs/wiki/Quick-Start.md')) {
    $text = Read-Text $relative
    if ($text -and -not $text.Contains($currentNeedle)) {
      Add-Failure "$relative does not contain current version marker $currentNeedle"
    }
  }
  $readme = Read-Text 'README.md'
  # Build the em dash from its code point so this script stays ASCII and the
  # pattern survives Windows PowerShell 5.1 reading BOM-less files as ANSI.
  $alphaOneResidue = 'v0\.11\.0-alpha\.1\s+' + [char]0x2014 + '\s+first internal alpha'
  if ($readme -match $alphaOneResidue -and $current -ne '0.11.0-alpha.1') {
    Add-Failure 'README.md still opens with alpha.1 current-release language.'
  }
  # Source annotations naming 0.11.0-alpha.3 are a drift problem only while
  # that release is absent from the recorded version history. Once the
  # release exists in history, references to it are legitimate historical
  # record and must not fail future version bumps.
  $historyVersions = @($version.history | ForEach-Object { [string]$_.version })
  if ($current -ne '0.11.0-alpha.3' -and $historyVersions -notcontains '0.11.0-alpha.3') {
    $alpha3 = @(Get-ChildItem -LiteralPath $root -Recurse -File -Force | Where-Object {
      -not (Test-ExcludedPath $_.FullName.Substring($root.Length).TrimStart('\','/')) -and ($_.FullName -ne $PSCommandPath) -and $_.Extension -in @('.cpp','.h','.hpp','.md','.json','.ps1','.txt','.xaml','.yml')
    } | Where-Object { ([System.IO.File]::ReadAllText($_.FullName) -match 'v?0\.11\.0-alpha\.3') })
    if ($alpha3.Count -gt 0) { Add-Failure "Repository current version is $current but alpha.3 references remain in $($alpha3.Count) file(s)." }
  }
}
# Non-protected attribution residue check. Product/client names are not banned; this checks only known residue phrases.
$protected = @(
  '.github/workflows/ai-contributor-guard.yml',
  '.github/copilot-instructions.md',
  'scripts/github_agents/check_no_ai_contributors.py',
  'scripts/github_agents/common.py',
  'scripts/Invoke-AiContributorGuard.ps1'
) | ForEach-Object { $_ -replace '/', [System.IO.Path]::DirectorySeparatorChar }
$residuePatterns = @('Copilot-review-hardened','Copilot-flagged','AI-scratch artifacts')
$scanFiles = Get-ChildItem -LiteralPath $root -Recurse -File -Force | Where-Object {
  $rel = $_.FullName.Substring($root.Length).TrimStart('\','/')
  $relNorm = $rel -replace '/', [System.IO.Path]::DirectorySeparatorChar
  ($protected -notcontains $relNorm) -and
  ($_.FullName -ne $PSCommandPath) -and
  ($_.Extension -notin @('.png','.ico','.bmp','.dll','.exe','.pdb','.lib','.obj','.pyc')) -and
  (-not (Test-ExcludedPath $rel))
}
foreach ($file in $scanFiles) {
  $text = [System.IO.File]::ReadAllText($file.FullName)
  foreach ($pattern in $residuePatterns) {
    if ($text.Contains($pattern)) {
      Add-Failure ("Attribution residue outside protected paths: $($file.FullName.Substring($root.Length).TrimStart('\','/')) contains '$pattern'")
    }
  }
}
foreach ($workflow in @('.github/workflows/release.yml','.github/workflows/windows-build-test-package.yml')) {
  $text = Read-Text $workflow
  if ($text -match '(?m)^\s*workflow_dispatch\s*:') { Add-Failure "$workflow must not contain workflow_dispatch." }
}
foreach ($path in @('.github/workflows/ai-contributor-guard.yml','.github/copilot-instructions.md','scripts/github_agents/check_no_ai_contributors.py','scripts/github_agents/common.py','scripts/Invoke-AiContributorGuard.ps1')) {
  if (-not (Test-Path -LiteralPath (Join-Path $root $path))) { Add-Failure "Protected path missing: $path" }
}
if ($failures.Count -gt 0) {
  Write-Error ("Repository metadata validation failed:`n" + ($failures -join "`n"))
  exit 1
}
Write-Host 'Repository metadata validation passed.'
exit 0
