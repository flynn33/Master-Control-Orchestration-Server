<#
.SYNOPSIS
  Checks local markdown links for broken targets.
#>
[CmdletBinding()]
param(
  [string]$RepoRoot = (Get-Location).Path,
  [string[]]$ExcludeDirectory = @('.git','build','dist','out','artifacts','Testing','TestResults','__pycache__'),
  [string[]]$AllowList = @()
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $RepoRoot).Path
$linkPattern = [regex]'(?<!!)\[[^\]\r\n]+\]\(([^)]+)\)'
$failures = New-Object System.Collections.Generic.List[string]
$total = 0
$files = Get-ChildItem -LiteralPath $root -Recurse -File -Force -Filter '*.md' | Where-Object {
  $relative = $_.FullName.Substring($root.Length).TrimStart('\','/')
  $parts = $relative -split '[\\/]'
  -not ($parts | Where-Object { $ExcludeDirectory -contains $_ })
}
$blankEvaluator = [System.Text.RegularExpressions.MatchEvaluator]{
  param($m) ($m.Value -replace '[^\r\n]', ' ')
}
foreach ($file in $files) {
  $text = [System.IO.File]::ReadAllText($file.FullName)
  # Blank fenced code blocks and inline code spans (preserving offsets so
  # reported line numbers stay accurate) so code samples that happen to
  # contain [x](y) shapes are not treated as markdown links.
  $text = [regex]::Replace($text, '(?ms)^[ \t]*```.*?^[ \t]*```[ \t]*$', $blankEvaluator)
  $text = [regex]::Replace($text, '`[^`\r\n]+`', $blankEvaluator)
  $matches = $linkPattern.Matches($text)
  foreach ($match in $matches) {
    $rawTarget = $match.Groups[1].Value.Trim()
    if ([string]::IsNullOrWhiteSpace($rawTarget)) { continue }
    if ($rawTarget.StartsWith('#')) { continue }
    if ($rawTarget -match '^[a-zA-Z][a-zA-Z0-9+.-]*:') { continue }
    $target = $rawTarget
    if ($target.StartsWith('<') -and $target.Contains('>')) {
      $target = $target.Substring(1, $target.IndexOf('>') - 1)
    }
    $targetNoAnchor = ($target -split '#', 2)[0]
    if ([string]::IsNullOrWhiteSpace($targetNoAnchor)) { continue }
    $decoded = [System.Uri]::UnescapeDataString($targetNoAnchor)
    $candidate = Join-Path $file.DirectoryName $decoded
    $candidates = New-Object System.Collections.Generic.List[string]
    $candidates.Add($candidate) | Out-Null
    if ([string]::IsNullOrEmpty([System.IO.Path]::GetExtension($candidate))) {
      $candidates.Add(($candidate + '.md')) | Out-Null
    }
    $exists = $false
    foreach ($item in $candidates) {
      if (Test-Path -LiteralPath $item) { $exists = $true; break }
    }
    $total++
    if (-not $exists) {
      $line = ($text.Substring(0, $match.Index).ToCharArray() | Where-Object { $_ -eq "`n" }).Count + 1
      $relativeFile = $file.FullName.Substring($root.Length).TrimStart('\','/')
      $record = '{0}:{1} -> {2}' -f $relativeFile, $line, $rawTarget
      if ($AllowList -notcontains $record) {
        $failures.Add($record) | Out-Null
      }
    }
  }
}
if ($failures.Count -gt 0) {
  Write-Error ("Markdown link validation failed:`n" + ($failures -join "`n"))
  exit 1
}
Write-Host ("Markdown link validation passed: {0} local link(s) checked." -f $total)
exit 0
