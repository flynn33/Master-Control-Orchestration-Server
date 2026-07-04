# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

Set-StrictMode -Version Latest

function Resolve-FirstExistingPath {
    param(
        [string[]]$Candidates,
        [string]$Label
    )

    foreach ($candidate in $Candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path $candidate)) {
            return $candidate
        }
    }

    $command = Get-Command $Label -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    throw "$Label was not found."
}

function Resolve-VisualStudioInstallationRoot {
    $environmentCandidates = @(
        $env:MASTERCONTROL_VS_INSTALL_ROOT,
        $env:VSINSTALLDIR
    )

    foreach ($candidate in $environmentCandidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path $candidate)) {
            return [System.IO.Path]::GetFullPath($candidate.TrimEnd('\'))
        }
    }

    $vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        # -prerelease lets `vswhere -latest` return a prerelease Visual Studio
        # (e.g. VS2026, which ships the v145 toolset the WinUI Shell targets)
        # instead of skipping it so an older stable VS2022 (v143) wins -latest.
        # This only matters on a mixed dev box with both installed; on the CI
        # runner VS2026 is the sole/preferred install and is selected regardless.
        $installationPath = (& $vswhere -latest -prerelease -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null | Select-Object -First 1)
        if (-not [string]::IsNullOrWhiteSpace($installationPath) -and (Test-Path $installationPath)) {
            return [System.IO.Path]::GetFullPath($installationPath.Trim())
        }
    }

    $fallbackCandidates = @(
        "C:\Program Files\Microsoft Visual Studio\18\Community",
        "C:\Program Files\Microsoft Visual Studio\18\BuildTools",
        "C:\Program Files\Microsoft Visual Studio\2022\Community",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
    )

    foreach ($candidate in $fallbackCandidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    throw "A supported Visual Studio installation was not found."
}

function Resolve-VcRuntimeDirectory {
    param(
        [Parameter(Mandatory = $true)]
        [string]$VsInstallRoot
    )

    $redistRoot = Join-Path $VsInstallRoot "VC\Redist\MSVC"
    if (-not (Test-Path $redistRoot)) {
        throw "Microsoft VC++ runtime root was not found at $redistRoot"
    }

    # Try newest toolset CRT directory first, then fall back to older ones.
    # Visual Studio 18 (VS2026) ships v145; VS2022 ships v143. Listing both
    # here keeps the script working on dev machines that haven't migrated yet
    # AND on the github-hosted windows-2025-vs2026 runner image that v0.10.14+
    # CI targets.
    $crtDirectoryNames = @(
        "Microsoft.VC145.CRT",  # VS2026 / VS18 / Platform Toolset v145
        "Microsoft.VC143.CRT"   # VS2022 / Platform Toolset v143
    )

    $candidates = @(Get-ChildItem -Path $redistRoot -Directory | Sort-Object Name -Descending)
    foreach ($candidate in $candidates) {
        foreach ($crtName in $crtDirectoryNames) {
            $crtDirectory = Join-Path $candidate.FullName "x64\$crtName"
            if (Test-Path $crtDirectory) {
                return $crtDirectory
            }
        }
    }

    throw "Microsoft VC++ x64 runtime directory was not found under $redistRoot (looked for: $($crtDirectoryNames -join ', '))"
}

function Resolve-MasterControlToolchain {
    $vsInstallRoot = Resolve-VisualStudioInstallationRoot
    $vsDevCmd = Resolve-FirstExistingPath -Candidates @(
        (Join-Path $vsInstallRoot "Common7\Tools\VsDevCmd.bat")
    ) -Label "VsDevCmd.bat"

    $cmake = Resolve-FirstExistingPath -Candidates @(
        (Join-Path $vsInstallRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe")
    ) -Label "cmake.exe"

    $ctest = Resolve-FirstExistingPath -Candidates @(
        (Join-Path $vsInstallRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe")
    ) -Label "ctest.exe"

    $vcpkgRoot = Resolve-FirstExistingPath -Candidates @(
        (Join-Path $vsInstallRoot "VC\vcpkg"),
        $env:VCPKG_ROOT
    ) -Label "vcpkg"

    $toolchainFile = Resolve-FirstExistingPath -Candidates @(
        (Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake")
    ) -Label "vcpkg.cmake"

    [pscustomobject][ordered]@{
        VsInstallRoot = $vsInstallRoot
        VsDevCmd = $vsDevCmd
        CMake = $cmake
        CTest = $ctest
        VcpkgRoot = $vcpkgRoot
        VcpkgToolchainFile = $toolchainFile
        VcRuntimeDirectory = Resolve-VcRuntimeDirectory -VsInstallRoot $vsInstallRoot
    }
}
