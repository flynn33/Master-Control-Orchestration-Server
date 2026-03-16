# Master Control Program
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

[CmdletBinding()]
param(
    [ValidateSet("debug", "release")]
    [string]$Preset = "debug"
)

$vsDevCmd = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
$cmake = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not (Test-Path $vsDevCmd)) {
    throw "VsDevCmd.bat not found at $vsDevCmd"
}

if (-not (Test-Path $cmake)) {
    throw "cmake.exe not found at $cmake"
}

$command = @(
    "call `"$vsDevCmd`" -host_arch=x64 -arch=x64",
    "set VCPKG_ROOT=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg",
    "cd /d `"$repoRoot`"",
    "`"$cmake`" --preset $Preset",
    "`"$cmake`" --build --preset $Preset",
    "`"$cmake`" --test --preset debug"
) -join " && "

cmd /c $command
if ($LASTEXITCODE -ne 0) {
    throw "Build pipeline failed with exit code $LASTEXITCODE"
}
