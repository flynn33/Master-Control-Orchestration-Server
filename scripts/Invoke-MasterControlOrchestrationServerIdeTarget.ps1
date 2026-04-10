# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Project,
    [ValidateSet("Debug", "Release", "MinSizeRel", "RelWithDebInfo")]
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$SolutionPath = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($SolutionPath)) {
    $SolutionPath = Join-Path $repoRoot "build\release\MasterControlOrchestrationServer.slnx"
}

. (Join-Path $PSScriptRoot "Resolve-MasterControlToolchain.ps1")

$toolchain = Resolve-MasterControlToolchain
$devenvCandidates = @(
    (Join-Path $toolchain.VsInstallRoot "Common7\IDE\devenv.com"),
    (Join-Path $toolchain.VsInstallRoot "Common7\IDE\devenv.exe")
)

$devenv = $devenvCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($devenv)) {
    throw "Visual Studio devenv executable was not found under $($toolchain.VsInstallRoot)."
}

$resolvedSolutionPath = [System.IO.Path]::GetFullPath($SolutionPath)
$arguments = @(
    $resolvedSolutionPath,
    "/Build", "$Configuration|$Platform",
    "/Project", $Project
)

$process = Start-Process `
    -FilePath $devenv `
    -ArgumentList $arguments `
    -Wait `
    -PassThru `
    -NoNewWindow

exit $process.ExitCode
