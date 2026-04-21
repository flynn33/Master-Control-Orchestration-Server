# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

<#
.SYNOPSIS
    Build the Master Control Orchestration Server Windows Installer (MSI) from
    an already-staged package directory.

.DESCRIPTION
    Called from Package-MasterControlOrchestrationServer.ps1 AFTER
    `cmake --install` has produced a flat payload tree under
    `dist/packages/<preset>/MasterControlOrchestrationServer-vX.Y.Z-win-x64/`.

    This script does three things:
      1. Scans the staged directory and generates a Files.wxs fragment with
         one Component per file, assigning explicit File Ids to the two
         executables the main .wxs references (`MasterControlShellExe` and
         `MasterControlBootstrapperExe`).
      2. Invokes `wix build` with all .wxs sources to produce a signed-ready
         MSI file.
      3. Returns a result object with the MSI path + version string.

    Requirements:
      - WiX v5 global tool (`dotnet tool install --global wix --version 5.0.2`)
      - WixToolset.UI.wixext and WixToolset.Util.wixext extensions installed:
        `wix extension add --global WixToolset.UI.wixext/5.0.2`
        `wix extension add --global WixToolset.Util.wixext/5.0.2`
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)] [string]$StageDirectory,
    [Parameter(Mandatory=$true)] [string]$Version,
    [Parameter(Mandatory=$true)] [string]$OutputMsiPath,
    [Parameter(Mandatory=$true)] [string]$IconsDir,
    [Parameter(Mandatory=$true)] [string]$PackagingDir,
    [Parameter(Mandatory=$true)] [string]$InstallerDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Resolve inputs
# ---------------------------------------------------------------------------
$StageDirectory = (Resolve-Path $StageDirectory).Path
$IconsDir       = (Resolve-Path $IconsDir).Path
$PackagingDir   = (Resolve-Path $PackagingDir).Path
$InstallerDir   = (Resolve-Path $InstallerDir).Path

Write-Host "==> Build-Msi.ps1"
Write-Host "    StageDirectory: $StageDirectory"
Write-Host "    Version:        $Version"
Write-Host "    OutputMsiPath:  $OutputMsiPath"
Write-Host "    IconsDir:       $IconsDir"
Write-Host "    PackagingDir:   $PackagingDir"

$wix = (Get-Command wix -ErrorAction Stop).Source
Write-Host "    wix:            $wix"
$wixVersion = (& $wix --version | Select-Object -Last 1)
Write-Host "    wix version:    $wixVersion"

# ---------------------------------------------------------------------------
# Version must be MAJOR.MINOR.PATCH.BUILD for MSI. Strip pre-release suffix.
# ---------------------------------------------------------------------------
$msiVersion = $Version -replace '-.*$', ''
$msiVersion = "$msiVersion.0"   # Make sure we have 4 parts (e.g. 0.4.3 -> 0.4.3.0)
Write-Host "    MSI version:    $msiVersion"

# ---------------------------------------------------------------------------
# Step 1: Harvest staged directory -> Files.wxs
# ---------------------------------------------------------------------------
# Files we exclude from the MSI payload (they stay in the zip-only distribution
# and are not needed inside the MSI-installed tree):
#   - INSTALL.txt, START-HERE.txt, PACKAGE-METADATA.json: zip-only docs
#   - Install-*.ps1, "Install Master Control...exe": zip-only installer paths
#   - *.preflight.json, preflight.log, *.pdb: debug / validation artifacts
#   - The MSI itself would never be here, but guard anyway
$excludeFileNames = @(
    'INSTALL.txt',
    'START-HERE.txt',
    'PACKAGE-METADATA.json',
    'Install-MasterControlOrchestrationServer.ps1',
    'Install Master Control Orchestration Server.exe',
    'MasterControlOrchestrationServerSetup.exe',
    'MasterControlOrchestrationServer-v*.preflight.json',
    'preflight.log',
    '*.pdb',
    '*.msi'
)

function Test-Excluded([string]$fileName) {
    foreach ($pattern in $excludeFileNames) {
        if ($fileName -like $pattern) { return $true }
    }
    return $false
}

# Stable component GUIDs: WiX will auto-generate ("*") for us. The component
# Id is a sanitized version of the file's relative path. We give the two
# "headline" executables explicit File Ids so the main .wxs + shortcut
# fragment can reference them as [#MasterControlOrchestrationServerExe] /
# [#MasterControlShellExe] / [#MasterControlBootstrapperExe].
$specialFileIds = @{
    'MasterControlOrchestrationServer.exe' = 'MasterControlOrchestrationServerExe'
    'MasterControlShell.exe'        = 'MasterControlShellExe'
    'MasterControlBootstrapper.exe' = 'MasterControlBootstrapperExe'
}

function Get-SafeId([string]$raw) {
    # WiX Ids must start with a letter/underscore and contain only ASCII
    # letters, digits, underscores, and periods, max 72 chars.
    $sanitized = ($raw -replace '[^A-Za-z0-9_.]', '_')
    if ($sanitized -match '^[^A-Za-z_]') { $sanitized = "f_$sanitized" }
    if ($sanitized.Length -gt 60) {
        # Long paths: hash-shorten while keeping readable prefix.
        $hash = [System.BitConverter]::ToString(
            [System.Security.Cryptography.SHA1]::Create().ComputeHash(
                [System.Text.Encoding]::UTF8.GetBytes($raw))).Replace('-', '').Substring(0, 10)
        $sanitized = "$($sanitized.Substring(0, 50))_$hash"
    }
    return $sanitized
}

# Build a directory -> files map.
$directoryMap = @{}
Get-ChildItem -Path $StageDirectory -Recurse -File |
    Where-Object { -not (Test-Excluded $_.Name) } |
    ForEach-Object {
        $relPath = $_.FullName.Substring($StageDirectory.Length + 1)
        $dir = Split-Path $relPath -Parent
        if (-not $dir) { $dir = '' }
        if (-not $directoryMap.ContainsKey($dir)) {
            $directoryMap[$dir] = New-Object System.Collections.ArrayList
        }
        [void]$directoryMap[$dir].Add([PSCustomObject]@{
            Name    = $_.Name
            RelPath = $relPath
            FullPath= $_.FullName
        })
    }

# Emit Files.wxs
$filesWxs = New-Object System.Text.StringBuilder
[void]$filesWxs.AppendLine('<?xml version="1.0" encoding="UTF-8"?>')
[void]$filesWxs.AppendLine('<!-- Auto-generated by installer/Build-Msi.ps1. Do not edit by hand. -->')
[void]$filesWxs.AppendLine('<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs">')
[void]$filesWxs.AppendLine('  <Fragment>')

# Sub-directory tree under INSTALLFOLDER
[void]$filesWxs.AppendLine('    <DirectoryRef Id="INSTALLFOLDER">')
$sortedDirs = ($directoryMap.Keys | Sort-Object)
$dirIds = @{ '' = 'INSTALLFOLDER' }
foreach ($dir in $sortedDirs) {
    if ($dir -eq '') { continue }
    # Build nested Directory elements. We write a flat list of Directory
    # elements with Id and FileSource pointing at the computed stage path.
    $parts = $dir -split '[\\/]'
    $accum = ''
    $parentId = 'INSTALLFOLDER'
    foreach ($part in $parts) {
        if (-not $part) { continue }
        if ($accum) { $accum = "$accum\$part" } else { $accum = $part }
        if (-not $dirIds.ContainsKey($accum)) {
            $dirIds[$accum] = "dir_" + (Get-SafeId $accum)
        }
    }
}

# Write nested Directory hierarchy (single pass, ordered).
# We emit them by walking paths depth-first so parents always come first.
$emitted = @{}
function Write-DirTree([string]$dir, [string]$indent) {
    if ($dir -eq '' -or $emitted.ContainsKey($dir)) { return }
    $parent = Split-Path $dir -Parent
    if ($parent) { Write-DirTree $parent ($indent + '  ') }
    $leaf = Split-Path $dir -Leaf
    $id = $dirIds[$dir]
    [void]$script:filesWxs.AppendLine("$indent<Directory Id=`"$id`" Name=`"$leaf`">")
    [void]$script:filesWxs.AppendLine("$indent</Directory>")
    $emitted[$dir] = $true
}

# Simpler linear emission — WiX allows DirectoryRef to contain flat Directory
# elements that use nested Id references. Actually simplest: emit each Dir
# with a self-contained path by nesting properly.
# Strategy: build a tree, recurse.
$tree = @{}
function Add-ToTree([hashtable]$node, [string[]]$parts) {
    if ($parts.Count -eq 0) { return }
    $head = $parts[0]
    if (-not $node.ContainsKey($head)) { $node[$head] = @{} }
    $rest = @($parts | Select-Object -Skip 1)
    Add-ToTree $node[$head] $rest
}
foreach ($dir in $sortedDirs) {
    if ($dir -eq '') { continue }
    $parts = $dir -split '[\\/]'
    Add-ToTree $tree @($parts)
}

function Write-TreeXml([hashtable]$node, [string]$prefix, [string]$indent) {
    $keys = $node.Keys | Sort-Object
    foreach ($k in $keys) {
        if ($prefix) { $path = "$prefix\$k" } else { $path = $k }
        $id = $dirIds[$path]
        $children = $node[$k]
        if ($children.Count -gt 0) {
            [void]$script:filesWxs.AppendLine("$indent<Directory Id=`"$id`" Name=`"$k`">")
            Write-TreeXml $children $path ("$indent  ")
            [void]$script:filesWxs.AppendLine("$indent</Directory>")
        } else {
            [void]$script:filesWxs.AppendLine("$indent<Directory Id=`"$id`" Name=`"$k`"/>")
        }
    }
}
Write-TreeXml $tree '' '      '
[void]$filesWxs.AppendLine('    </DirectoryRef>')

# Emit one ComponentGroup with a Component per file, each assigned to its
# Directory via DirectoryRef syntax ("Directory=<id>").
[void]$filesWxs.AppendLine('    <ComponentGroup Id="MasterControlPayload">')

$seenIds = @{}
$componentIndex = 0
foreach ($dir in $sortedDirs) {
    $files = $directoryMap[$dir]
    $dirId = $dirIds[$dir]
    foreach ($f in $files) {
        $componentIndex++
        $componentId = "cmp_$componentIndex`_" + (Get-SafeId $f.RelPath)
        if ($seenIds.ContainsKey($componentId)) { continue }
        $seenIds[$componentId] = $true

        $fileId = $null
        if ($specialFileIds.ContainsKey($f.Name)) { $fileId = $specialFileIds[$f.Name] }

        $relForwardSlash = $f.RelPath.Replace('\', '/')
        [void]$filesWxs.Append("      <Component Id=`"$componentId`" Directory=`"$dirId`" Guid=`"*`">")
        if ($fileId) {
            [void]$filesWxs.Append("<File Id=`"$fileId`" Source=`"`$(var.StageDir)\$($f.RelPath)`" KeyPath=`"yes`"/>")
        } else {
            [void]$filesWxs.Append("<File Source=`"`$(var.StageDir)\$($f.RelPath)`" KeyPath=`"yes`"/>")
        }
        [void]$filesWxs.AppendLine("</Component>")
    }
}

[void]$filesWxs.AppendLine('    </ComponentGroup>')
[void]$filesWxs.AppendLine('  </Fragment>')
[void]$filesWxs.AppendLine('</Wix>')

$filesWxsPath = Join-Path $InstallerDir 'Fragments\Files.wxs'
[System.IO.File]::WriteAllText($filesWxsPath, $filesWxs.ToString(), (New-Object System.Text.UTF8Encoding $false))
$fileCount = ($directoryMap.Values | ForEach-Object { $_.Count } | Measure-Object -Sum).Sum
Write-Host "    Files.wxs:      $filesWxsPath  ($fileCount files across $($sortedDirs.Count) dirs)"

# ---------------------------------------------------------------------------
# Step 2: Build the MSI
# ---------------------------------------------------------------------------
$wxsSources = @(
    (Join-Path $InstallerDir 'MasterControlOrchestrationServer.wxs'),
    (Join-Path $InstallerDir 'Fragments\Files.wxs'),
    (Join-Path $InstallerDir 'Fragments\ShortcutsFragment.wxs'),
    (Join-Path $InstallerDir 'Fragments\CustomActions.wxs')
)

$wixArgs = @(
    'build'
    '-arch'; 'x64'
    '-out'; $OutputMsiPath
    '-ext'; 'WixToolset.UI.wixext'
    '-ext'; 'WixToolset.Util.wixext'
    '-d'; "ProductVersion=$msiVersion"
    '-d'; "StageDir=$StageDirectory"
    '-d'; "IconsDir=$IconsDir"
    '-d'; "PackagingDir=$PackagingDir"
    '-d'; "InstallerDir=$InstallerDir"
) + $wxsSources

Write-Host "==> wix $($wixArgs -join ' ')"
& $wix @wixArgs | Out-Host
if ($LASTEXITCODE -ne 0) { throw "wix build failed with exit code $LASTEXITCODE" }

$msiItem = Get-Item $OutputMsiPath
Write-Host "==> MSI built: $($msiItem.FullName)  ($([math]::Round($msiItem.Length / 1MB, 1)) MB)"

return [PSCustomObject]@{
    MsiPath    = $msiItem.FullName
    MsiVersion = $msiVersion
    FileCount  = $fileCount
}
