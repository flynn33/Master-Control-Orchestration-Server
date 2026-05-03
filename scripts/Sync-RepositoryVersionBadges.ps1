# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

<#
.SYNOPSIS
Keeps README.md badges, vcpkg.json version-string, and VERSION.json in sync.

.DESCRIPTION
VERSION.json is the single source of truth for the product version. This script
reads VERSION.json and regenerates:
  - the README version + released badges at the top of README.md
  - the "Current release" block further down in README.md
  - the version-string field in vcpkg.json

Run in -CheckOnly mode in CI to fail the build if badges drift.
#>

[CmdletBinding()]
param(
    [switch]$CheckOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$versionPath = Join-Path $repoRoot 'VERSION.json'
$readmePath = Join-Path $repoRoot 'README.md'
$vcpkgPath = Join-Path $repoRoot 'vcpkg.json'

if (-not (Test-Path $versionPath)) {
    throw "VERSION.json not found at $versionPath"
}

$version = Get-Content $versionPath -Raw | ConvertFrom-Json
$current = $version.current_version
$released = $version.released_at

if (-not $current -or -not $released) {
    throw "VERSION.json is missing current_version or released_at."
}

$badgeVersion = $current -replace '-', '--'
$badgeReleased = $released -replace '-', '--'

# Build the badge line using [char] escapes for non-ASCII glyphs so this
# script file itself remains ASCII and stays immune to PowerShell's codepage
# assumptions when it loads the script source.
$middleDot = [char]0x00B7
$emDash = [char]0x2014

$expectedTopLine = "![version](https://img.shields.io/badge/version-v$badgeVersion-00f6ff?style=flat-square) ![released](https://img.shields.io/badge/released-$badgeReleased-031018?style=flat-square) ![platform](https://img.shields.io/badge/platform-Windows%2011%20/%20Server%202022-0a1018?style=flat-square) ![toolchain](https://img.shields.io/badge/toolchain-C++20%20${middleDot}%20WinUI%203%20${middleDot}%20CMake-00aacc?style=flat-square) ![license](https://img.shields.io/badge/license-Proprietary-5a00e8?style=flat-square)"
$expectedCurrentLine = "- **Current release:** ``v$current`` ($released)"

function Escape-RegexReplacement {
    param([string]$Text)
    # In .NET regex replacement strings, $ must be doubled to render literally.
    return $Text -replace '\$', '$$$$'
}

# Read and write text as UTF-8 without BOM so we never corrupt non-ASCII glyphs
# like em dashes or middle dots in the README. [System.IO.File]::ReadAllText
# auto-detects encoding and returns .NET strings; WriteAllText with
# UTF8Encoding($false) writes UTF-8 without BOM.
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

$readme = [System.IO.File]::ReadAllText($readmePath)
$readmeUpdated = $readme

$readmeUpdated = [regex]::Replace(
    $readmeUpdated,
    '!\[version\]\(https://img\.shields\.io/badge/version-[^\)]+\)[^\r\n]+',
    (Escape-RegexReplacement $expectedTopLine)
)

$readmeUpdated = [regex]::Replace(
    $readmeUpdated,
    '- \*\*Current release:\*\* `v[^`]+` \([0-9\-]+\)',
    (Escape-RegexReplacement $expectedCurrentLine)
)

# Rewrite the entire "## Current release" block from the latest history entry
# in VERSION.json. The block runs from the "## Current release" heading up to
# (but not including) the next "---" horizontal rule.
$latest = $version.history[0]
$summary = $latest.summary
$entries = @($latest.entries)

$entryLines = ($entries | ForEach-Object { "- $_" }) -join "`r`n"

$expectedBlock = @"
## Current release

**``v$current`` $emDash $released**

$summary

$entryLines
"@

$readmeUpdated = [regex]::Replace(
    $readmeUpdated,
    '## Current release\r?\n.*?(?=\r?\n---)',
    (Escape-RegexReplacement $expectedBlock),
    [System.Text.RegularExpressions.RegexOptions]::Singleline
)

$readmeChanged = ($readmeUpdated -ne $readme)

$vcpkgRaw = [System.IO.File]::ReadAllText($vcpkgPath)
$vcpkg = $vcpkgRaw | ConvertFrom-Json
$vcpkgVersionDrift = ($vcpkg.'version-string' -ne $current)

$vcpkgUpdated = $vcpkgRaw
if ($vcpkgVersionDrift) {
    $vcpkgUpdated = [regex]::Replace(
        $vcpkgRaw,
        '"version-string"\s*:\s*"[^"]*"',
        (Escape-RegexReplacement ('"version-string": "' + $current + '"'))
    )
}

# Keep the EXE VERSIONINFO blocks in lock-step with VERSION.json. The .rc
# files for Shell / ServiceHost / Bootstrapper hardcode FILEVERSION,
# PRODUCTVERSION, and the FileVersion / ProductVersion string values; we
# rewrite all four on each version bump rather than threading a generated
# header through the WinUI MSBuild project (which has its own include path
# resolution that doesn't see the CMake binary tree).
$rcPaths = @(
    (Join-Path $repoRoot 'src\MasterControlShell\MasterControlShell.rc'),
    (Join-Path $repoRoot 'src\MasterControlServiceHost\MasterControlServiceHost.rc'),
    (Join-Path $repoRoot 'src\MasterControlBootstrapper\MasterControlBootstrapper.rc')
)
$rcCommaVersion = ($current -replace '-.*$', '') -replace '\.', ',' # 0.6.3 -> 0,6,3
$rcDotVersion   = ($current -replace '-.*$', '')                    # 0.6.3
# Ensure four numeric components for FILEVERSION (MAJOR,MINOR,PATCH,BUILD).
if (($rcCommaVersion -split ',').Count -lt 4) {
    $rcCommaVersion = $rcCommaVersion + ',0' * (4 - ($rcCommaVersion -split ',').Count)
}
if (($rcDotVersion -split '\.').Count -lt 4) {
    $rcDotVersion = $rcDotVersion + '.0' * (4 - ($rcDotVersion -split '\.').Count)
}
$rcChanges = @{}
foreach ($rcPath in $rcPaths) {
    if (-not (Test-Path $rcPath)) { continue }
    $rcRaw = [System.IO.File]::ReadAllText($rcPath)
    $rcUpdated = $rcRaw
    $rcUpdated = [regex]::Replace(
        $rcUpdated,
        '(FILEVERSION\s+)\d+\s*,\s*\d+\s*,\s*\d+\s*,\s*\d+',
        (Escape-RegexReplacement ('${1}' + $rcCommaVersion))
    )
    $rcUpdated = [regex]::Replace(
        $rcUpdated,
        '(PRODUCTVERSION\s+)\d+\s*,\s*\d+\s*,\s*\d+\s*,\s*\d+',
        (Escape-RegexReplacement ('${1}' + $rcCommaVersion))
    )
    $rcUpdated = [regex]::Replace(
        $rcUpdated,
        '(VALUE\s+"FileVersion",\s+)"[0-9.]+"',
        (Escape-RegexReplacement ('${1}"' + $rcDotVersion + '"'))
    )
    $rcUpdated = [regex]::Replace(
        $rcUpdated,
        '(VALUE\s+"ProductVersion",\s+)"[0-9.]+"',
        (Escape-RegexReplacement ('${1}"' + $rcDotVersion + '"'))
    )
    if ($rcUpdated -ne $rcRaw) {
        $rcChanges[$rcPath] = $rcUpdated
    }
}

if ($CheckOnly) {
    $drift = @()
    if ($readmeChanged) { $drift += 'README.md badge or current-release line' }
    if ($vcpkgVersionDrift) { $drift += 'vcpkg.json version-string' }
    if ($rcChanges.Count -gt 0) { $drift += "$($rcChanges.Count) .rc VERSIONINFO block(s)" }
    if ($drift.Count -gt 0) {
        Write-Error ("Version drift detected in: " + ($drift -join ', ') + ". Run Sync-RepositoryVersionBadges.ps1 to fix.")
        exit 1
    }
    Write-Host "Version badges are in sync with VERSION.json ($current)."
    exit 0
}

if ($readmeChanged) {
    [System.IO.File]::WriteAllText($readmePath, $readmeUpdated, $utf8NoBom)
    Write-Host "Updated README.md"
}
if ($vcpkgVersionDrift) {
    [System.IO.File]::WriteAllText($vcpkgPath, $vcpkgUpdated, $utf8NoBom)
    Write-Host "Updated vcpkg.json version-string to $current"
}
foreach ($rcPath in $rcChanges.Keys) {
    [System.IO.File]::WriteAllText($rcPath, $rcChanges[$rcPath], $utf8NoBom)
    Write-Host ("Updated VERSIONINFO in " + (Split-Path -Leaf $rcPath))
}
if (-not $readmeChanged -and -not $vcpkgVersionDrift -and $rcChanges.Count -eq 0) {
    Write-Host "Already in sync ($current)."
}
