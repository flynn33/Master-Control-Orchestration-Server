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

function Invoke-DevShell {
    param([string[]]$Commands)

    $chain = @(
        "call `"$vsDevCmd`" -host_arch=x64 -arch=x64",
        "set `"VCPKG_ROOT=$vcpkgRoot`"",
        "cd /d `"$repoRoot`""
    ) + $Commands

    cmd /c ($chain -join " && ")
    if ($LASTEXITCODE -ne 0) {
        throw "Build pipeline failed with exit code $LASTEXITCODE"
    }
}

Invoke-DevShell -Commands @(
    "`"$cmake`" --preset $Preset",
    "`"$cmake`" --build --preset $Preset",
    "`"$ctest`" --preset $Preset"
)
