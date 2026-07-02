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

# Build strings using [char] escapes for non-ASCII glyphs so this script file
# itself remains ASCII and stays immune to PowerShell's codepage assumptions
# when it loads the script source.
$emDash = [char]0x2014

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

# Rewrite the version and released badge values in place. The badges may sit
# on one combined line or on individual lines; replacing the URL segment
# handles both layouts.
$readmeUpdated = [regex]::Replace(
    $readmeUpdated,
    'badge/version-v[^-]*(?:--[^-]*)*-00f6ff',
    (Escape-RegexReplacement "badge/version-v$badgeVersion-00f6ff")
)

$readmeUpdated = [regex]::Replace(
    $readmeUpdated,
    'badge/released-[0-9]{4}--[0-9]{2}--[0-9]{2}-031018',
    (Escape-RegexReplacement "badge/released-$badgeReleased-031018")
)

$readmeUpdated = [regex]::Replace(
    $readmeUpdated,
    '- \*\*Current release:\*\* `v[^`]+` \([0-9\-]+\)',
    (Escape-RegexReplacement $expectedCurrentLine)
)

# Rewrite the entire "## Current release" block from the latest history entry
# in VERSION.json. The block runs from the "## Current release" heading up to
# (but not including) the next "---" horizontal rule. Generate the block with
# the README's own dominant newline so the rewrite is stable on both CRLF and
# LF checkouts.
$latest = $version.history[0]
$summary = $latest.summary
$entries = @($latest.entries)

$newline = if ($readme.Contains("`r`n")) { "`r`n" } else { "`n" }
$entryLines = ($entries | ForEach-Object { "- $_" }) -join $newline

$expectedBlock = (@(
    '## Current release',
    '',
    "**``v$current`` $emDash $released**",
    '',
    $summary,
    '',
    $entryLines
) -join $newline)

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

# Keep the EXE VERSIONINFO blocks on generated VERSION.json-derived macros.
# CMake emits MasterControlVersion.h for CMake-built targets and
# src\MasterControlShell\GeneratedVersion.h for the WinUI MSBuild target; this
# script now treats numeric VERSIONINFO literals as drift instead of rewriting
# them, because the product version should flow through generated headers.
$rcPaths = @(
    (Join-Path $repoRoot 'src\MasterControlShell\MasterControlShell.rc'),
    (Join-Path $repoRoot 'src\MasterControlServiceHost\MasterControlServiceHost.rc'),
    (Join-Path $repoRoot 'src\MasterControlBootstrapper\MasterControlBootstrapper.rc')
)
$rcLiteralDrift = @()
foreach ($rcPath in $rcPaths) {
    if (-not (Test-Path $rcPath)) { continue }
    $rcRaw = [System.IO.File]::ReadAllText($rcPath)
    if ($rcRaw -match 'FILEVERSION\s+\d+\s*,\s*\d+\s*,\s*\d+\s*,\s*\d+' -or
        $rcRaw -match 'PRODUCTVERSION\s+\d+\s*,\s*\d+\s*,\s*\d+\s*,\s*\d+' -or
        $rcRaw -match 'VALUE\s+"(?:FileVersion|ProductVersion)",\s+"[0-9.]+"') {
        $rcLiteralDrift += (Split-Path -Leaf $rcPath)
    }
}

# Current-release coverage in hand-authored docs. These are verified, not
# rewritten, so hand-authored context stays hand-authored; drift fails
# -CheckOnly and must be fixed by editing the doc.
$docDrift = @()

if (-not $readme.Contains("> **v$current")) {
    $docDrift += "README.md top release paragraph does not open with v$current"
}

$homeDocPath = Join-Path $repoRoot (Join-Path 'docs' (Join-Path 'wiki' 'Home.md'))
if (Test-Path $homeDocPath) {
    $homeDoc = [System.IO.File]::ReadAllText($homeDocPath)
    if (-not $homeDoc.Contains("badge/version-v$badgeVersion-")) { $docDrift += "docs/wiki/Home.md version badge does not match v$current" }
    if (-not $homeDoc.Contains("| Version | ``v$current`` |")) { $docDrift += "docs/wiki/Home.md current-release table does not list v$current" }
}

$versionsDocPath = Join-Path $repoRoot (Join-Path 'docs' (Join-Path 'wiki' 'Versions.md'))
if (Test-Path $versionsDocPath) {
    $versionsDoc = [System.IO.File]::ReadAllText($versionsDocPath)
    if (-not $versionsDoc.Contains("badge/current-v$badgeVersion-")) { $docDrift += "docs/wiki/Versions.md current badge does not match v$current" }
    if (-not $versionsDoc.Contains("| **Version** | ``v$current`` |")) { $docDrift += "docs/wiki/Versions.md current-release table does not list v$current" }
}

$quickStartPath = Join-Path $repoRoot (Join-Path 'docs' (Join-Path 'wiki' 'Quick-Start.md'))
if (Test-Path $quickStartPath) {
    $quickStart = [System.IO.File]::ReadAllText($quickStartPath)
    if (-not $quickStart.Contains("MasterControlOrchestrationServer-v$current-win-x64")) {
        $docDrift += "docs/wiki/Quick-Start.md package examples do not use the v$current package name"
    }
}

$changelogPath = Join-Path $repoRoot 'CHANGELOG.md'
if (Test-Path $changelogPath) {
    $changelog = [System.IO.File]::ReadAllText($changelogPath)
    $headingMatch = [regex]::Match($changelog, '(?m)^## \[(?!Unreleased)([^\]]+)\]')
    if (-not $headingMatch.Success -or $headingMatch.Groups[1].Value -ne $current) {
        $observed = if ($headingMatch.Success) { $headingMatch.Groups[1].Value } else { '(none)' }
        $docDrift += "CHANGELOG.md top release section is '$observed', expected '$current'"
    }
}

if ($CheckOnly) {
    $drift = @()
    if ($readmeChanged) { $drift += 'README.md badge or current-release line' }
    if ($vcpkgVersionDrift) { $drift += 'vcpkg.json version-string' }
    if ($rcLiteralDrift.Count -gt 0) { $drift += ('numeric .rc VERSIONINFO literal(s): ' + ($rcLiteralDrift -join ', ')) }
    $drift += $docDrift
    if ($drift.Count -gt 0) {
        Write-Error ("Version drift detected in: " + ($drift -join '; ') + ". Run Sync-RepositoryVersionBadges.ps1 and update the listed docs to fix.")
        exit 1
    }
    Write-Host "Version badges are in sync with VERSION.json ($current)."
    exit 0
}

if ($docDrift.Count -gt 0) {
    Write-Warning ("Hand-authored docs drift (fix manually): " + ($docDrift -join '; '))
}

if ($readmeChanged) {
    [System.IO.File]::WriteAllText($readmePath, $readmeUpdated, $utf8NoBom)
    Write-Host "Updated README.md"
}
if ($vcpkgVersionDrift) {
    [System.IO.File]::WriteAllText($vcpkgPath, $vcpkgUpdated, $utf8NoBom)
    Write-Host "Updated vcpkg.json version-string to $current"
}
if ($rcLiteralDrift.Count -gt 0) {
    throw ('Numeric VERSIONINFO literals remain in .rc files: ' + ($rcLiteralDrift -join ', ') + '. Replace them with generated version macros.')
}
if (-not $readmeChanged -and -not $vcpkgVersionDrift -and $rcLiteralDrift.Count -eq 0) {
    Write-Host "Already in sync ($current)."
}
