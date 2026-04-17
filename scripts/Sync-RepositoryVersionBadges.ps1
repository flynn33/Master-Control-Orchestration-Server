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

$expectedTopLine = "![version](https://img.shields.io/badge/version-v$badgeVersion-00f6ff?style=flat-square) ![released](https://img.shields.io/badge/released-$badgeReleased-031018?style=flat-square) ![platform](https://img.shields.io/badge/platform-Windows%2011%20/%20Server%202022-0a1018?style=flat-square) ![toolchain](https://img.shields.io/badge/toolchain-C++20%20·%20WinUI%203%20·%20CMake-00aacc?style=flat-square) ![license](https://img.shields.io/badge/license-Proprietary-5a00e8?style=flat-square)"
$expectedCurrentLine = "- **Current release:** ``v$current`` ($released)"

$readme = Get-Content $readmePath -Raw
$readmeUpdated = $readme

$readmeUpdated = [regex]::Replace(
    $readmeUpdated,
    '!\[version\]\(https://img\.shields\.io/badge/version-[^\)]+\)[^\r\n]+',
    [System.Text.RegularExpressions.Regex]::Escape($expectedTopLine) -replace '\\(.)', '$1'
)

$readmeUpdated = [regex]::Replace(
    $readmeUpdated,
    '- \*\*Current release:\*\* `v[^`]+` \([0-9\-]+\)',
    [System.Text.RegularExpressions.Regex]::Escape($expectedCurrentLine) -replace '\\(.)', '$1'
)

$readmeChanged = ($readmeUpdated -ne $readme)

$vcpkgRaw = Get-Content $vcpkgPath -Raw
$vcpkg = $vcpkgRaw | ConvertFrom-Json
$vcpkgVersionDrift = ($vcpkg.'version-string' -ne $current)

$vcpkgUpdated = $vcpkgRaw
if ($vcpkgVersionDrift) {
    $vcpkgUpdated = [regex]::Replace(
        $vcpkgRaw,
        '"version-string"\s*:\s*"[^"]*"',
        '"version-string": "' + $current + '"'
    )
}

if ($CheckOnly) {
    $drift = @()
    if ($readmeChanged) { $drift += 'README.md badge or current-release line' }
    if ($vcpkgVersionDrift) { $drift += 'vcpkg.json version-string' }
    if ($drift.Count -gt 0) {
        Write-Error ("Version drift detected in: " + ($drift -join ', ') + ". Run Sync-RepositoryVersionBadges.ps1 to fix.")
        exit 1
    }
    Write-Host "Version badges are in sync with VERSION.json ($current)."
    exit 0
}

if ($readmeChanged) {
    Set-Content -Path $readmePath -Value $readmeUpdated -NoNewline:$false -Encoding UTF8
    Write-Host "Updated README.md"
}
if ($vcpkgVersionDrift) {
    Set-Content -Path $vcpkgPath -Value $vcpkgUpdated -NoNewline:$false -Encoding UTF8
    Write-Host "Updated vcpkg.json version-string to $current"
}
if (-not $readmeChanged -and -not $vcpkgVersionDrift) {
    Write-Host "Already in sync ($current)."
}
