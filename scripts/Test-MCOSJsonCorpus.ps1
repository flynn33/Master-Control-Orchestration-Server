<#
.SYNOPSIS
  Validates all JSON files in the repository.
#>
[CmdletBinding()]
param(
  [string]$RepoRoot = (Get-Location).Path,
  [string[]]$ExcludeDirectory = @('.git','build','dist','out','artifacts','Testing','TestResults','__pycache__')
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $RepoRoot).Path
$failures = New-Object System.Collections.Generic.List[string]
$files = Get-ChildItem -LiteralPath $root -Recurse -File -Force -Filter '*.json' | Where-Object {
  $relative = $_.FullName.Substring($root.Length).TrimStart('\','/')
  $parts = $relative -split '[\\/]'
  -not ($parts | Where-Object { $ExcludeDirectory -contains $_ })
}
foreach ($file in $files) {
  try {
    $raw = [System.IO.File]::ReadAllText($file.FullName)
    $null = $raw | ConvertFrom-Json -ErrorAction Stop
  } catch {
    $failures.Add(('{0}: {1}' -f $file.FullName.Substring($root.Length).TrimStart('\','/'), $_.Exception.Message)) | Out-Null
  }
}
if ($failures.Count -gt 0) {
  Write-Error ("JSON corpus validation failed:`n" + ($failures -join "`n"))
  exit 1
}
Write-Host ("JSON corpus validation passed: {0} file(s)." -f $files.Count)
exit 0
