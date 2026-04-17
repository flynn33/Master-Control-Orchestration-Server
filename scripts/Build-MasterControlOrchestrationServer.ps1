# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

[CmdletBinding()]
param(
    [ValidateSet("debug", "release")]
    [string]$Preset = "debug"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "Resolve-MasterControlToolchain.ps1")

$toolchain = Resolve-MasterControlToolchain
$vsDevCmd = $toolchain.VsDevCmd
$cmake = $toolchain.CMake
$ctest = $toolchain.CTest
$vcpkgRoot = $toolchain.VcpkgRoot

function Enter-McDevShell {
    # Use VS's own Launch-VsDevShell.ps1 to set up the developer environment
    # inside the current PowerShell session. This handles vswhere discovery,
    # MSBuild location, and Windows SDK paths natively — no `cmd /c` chain
    # and no 8191-character command-line limit.
    $launchScript = Join-Path (Split-Path -Parent $vsDevCmd) 'Launch-VsDevShell.ps1'
    if (-not (Test-Path $launchScript)) {
        throw "Could not find Launch-VsDevShell.ps1 near VsDevCmd.bat at $vsDevCmd."
    }

    # The script defaults to x86 host arch; override explicitly so we stay on x64.
    & $launchScript -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null

    # Ensure the Windows 10 SDK bin (containing mdmerge.exe and friends) is
    # on PATH. Some VS installs only add it conditionally; we prepend the
    # latest installed SDK version to be safe.
    $sdkBinRoot = 'C:\Program Files (x86)\Windows Kits\10\bin'
    if (Test-Path $sdkBinRoot) {
        $sdkBinPath = (Get-ChildItem -Path $sdkBinRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
            Sort-Object { [Version]$_.Name } -Descending |
            Select-Object -First 1 -ExpandProperty FullName)
        if ($sdkBinPath) {
            $env:PATH = (Join-Path $sdkBinPath 'x64') + ';' + $env:PATH
        }
    }

    $env:VCPKG_ROOT = $vcpkgRoot
    Set-Location $repoRoot
}

function Invoke-DevShell {
    param([string[]]$Commands)

    Enter-McDevShell

    # Sanity-check the env we hand off to the build: if mdmerge is not
    # findable here, the WinUI shell custom build will fail deep inside
    # CppWinRT.targets with a misleading error. Log once so failures are
    # obvious.
    $mdm = Get-Command mdmerge.exe -ErrorAction SilentlyContinue
    $xcp = Get-Command xcopy.exe -ErrorAction SilentlyContinue
    Write-Host "Invoke-DevShell: mdmerge=$(if ($mdm) { $mdm.Source } else { '<missing>' })"
    Write-Host "Invoke-DevShell: xcopy=$(if ($xcp) { $xcp.Source } else { '<missing>' })"

    foreach ($cmd in $Commands) {
        # Use cmd /c so the build command strings (with "--preset", quoted
        # paths, etc.) are parsed the way a developer prompt would parse
        # them. The child cmd inherits $env:PATH from this PowerShell
        # session, which Launch-VsDevShell has already populated.
        cmd /c $cmd
        if ($LASTEXITCODE -ne 0) {
            throw "Build pipeline failed with exit code $LASTEXITCODE"
        }
    }
}

Invoke-DevShell -Commands @(
    "`"$cmake`" --preset $Preset",
    "`"$cmake`" --build --preset $Preset",
    "`"$ctest`" --preset $Preset"
)
