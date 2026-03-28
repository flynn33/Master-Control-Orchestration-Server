# Master Control Program
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

[CmdletBinding()]
param(
    [ValidateSet("debug", "release")]
    [string]$Preset = "release",
    [string]$Version = "",
    [string]$OutputRoot = "",
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [switch]$KeepStage
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$configuration = if ($Preset -eq "debug") { "Debug" } else { "Release" }
$binaryDir = Join-Path $repoRoot ("build\" + $Preset)

function Resolve-FirstPath {
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

function Invoke-DevShell {
    param([string[]]$Commands)

    $chain = @(
        "call `"$script:vsDevCmd`" -host_arch=x64 -arch=x64",
        "set VCPKG_ROOT=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg",
        "cd /d `"$script:repoRoot`""
    ) + $Commands

    cmd /c ($chain -join " && ")
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE"
    }
}

function Invoke-CapturedProcess {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    $stdout = [System.IO.Path]::GetTempFileName()
    $stderr = [System.IO.Path]::GetTempFileName()

    try {
        $process = Start-Process `
            -FilePath $FilePath `
            -ArgumentList $Arguments `
            -Wait `
            -PassThru `
            -NoNewWindow `
            -RedirectStandardOutput $stdout `
            -RedirectStandardError $stderr

        return [pscustomobject]@{
            exitCode = $process.ExitCode
            stdout = (Get-Content $stdout -Raw)
            stderr = (Get-Content $stderr -Raw)
        }
    } finally {
        if (Test-Path $stdout) { Remove-Item $stdout -Force }
        if (Test-Path $stderr) { Remove-Item $stderr -Force }
    }
}

$vsDevCmd = Resolve-FirstPath -Candidates @(
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
) -Label "VsDevCmd.bat"

$cmake = Resolve-FirstPath -Candidates @(
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
) -Label "cmake.exe"

$ctest = Resolve-FirstPath -Candidates @(
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe",
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
) -Label "ctest.exe"

if ([string]::IsNullOrWhiteSpace($Version)) {
    $versionDocument = Get-Content (Join-Path $repoRoot "VERSION.json") -Raw | ConvertFrom-Json
    $Version = $versionDocument.current_version
}

$normalizedVersion = if ($Version.StartsWith("v")) { $Version.Substring(1) } else { $Version }
$versionTag = "v$normalizedVersion"
$gitCommit = (git -C $repoRoot rev-parse --short HEAD).Trim()

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot ("dist\packages\" + $Preset)
}

$packageName = "MasterControlProgram-$versionTag-win-x64"
if ($Preset -ne "release") {
    $packageName += "-$Preset"
}

$stageDirectory = Join-Path $OutputRoot $packageName
$zipPath = Join-Path $OutputRoot ($packageName + ".zip")
$validationPath = Join-Path $OutputRoot ($packageName + ".preflight.json")
$metadataPath = Join-Path $stageDirectory "PACKAGE-METADATA.json"
$instructionsPath = Join-Path $stageDirectory "INSTALL.txt"
$validationTarget = Join-Path $OutputRoot ($packageName + ".validation-target")

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
if ((Test-Path $stageDirectory) -and -not $KeepStage) {
    Remove-Item -Path $stageDirectory -Recurse -Force
}
if (Test-Path $validationTarget) {
    Remove-Item -Path $validationTarget -Recurse -Force
}

if (-not $SkipBuild) {
    Invoke-DevShell -Commands @(
        "`"$cmake`" --preset $Preset",
        "`"$cmake`" --build --preset $Preset"
    )
}

if (-not $SkipTests) {
    if ($Preset -eq "debug") {
        Invoke-DevShell -Commands @(
            "`"$cmake`" --test --preset debug"
        )
    } else {
        Invoke-DevShell -Commands @(
            "`"$ctest`" --test-dir `"$binaryDir`" -C $configuration --output-on-failure"
        )
    }
}

Invoke-DevShell -Commands @(
    "`"$cmake`" --install `"$binaryDir`" --config $configuration --prefix `"$stageDirectory`""
)

$instructions = @"
Master Control Program $versionTag

Package root: $packageName
Build configuration: $configuration
Commit: $gitCommit

Quick start
1. Extract this package to a writable local folder.
2. Open PowerShell in the extracted folder.
3. Run a preflight check before installing.

Fully managed install (requires Administrator)
.\MasterControlBootstrapper.exe preflight "C:\Program Files\Master Control Program" --json
.\MasterControlBootstrapper.exe install "C:\Program Files\Master Control Program" --json

Non-admin test install
.\MasterControlBootstrapper.exe preflight "$env:LOCALAPPDATA\MasterControlProgram" --skip-service --skip-firewall --skip-uninstall-registration --json
.\MasterControlBootstrapper.exe install "$env:LOCALAPPDATA\MasterControlProgram" --skip-service --skip-firewall --skip-uninstall-registration --json

Validation
.\MasterControlBootstrapper.exe validate "C:\Program Files\Master Control Program" --json

Uninstall
.\MasterControlBootstrapper.exe uninstall "C:\Program Files\Master Control Program" --json
"@
Set-Content -Path $instructionsPath -Value $instructions -Encoding UTF8

$preflightResult = Invoke-CapturedProcess -FilePath (Join-Path $stageDirectory "MasterControlBootstrapper.exe") -Arguments @(
    "preflight",
    $validationTarget,
    "--skip-service",
    "--skip-firewall",
    "--skip-uninstall-registration",
    "--json"
)

if ($preflightResult.exitCode -ne 0) {
    throw "Packaged bootstrapper preflight failed: $($preflightResult.stderr)$($preflightResult.stdout)"
}

$preflightJson = $null
if (-not [string]::IsNullOrWhiteSpace($preflightResult.stdout)) {
    $preflightJson = $preflightResult.stdout | ConvertFrom-Json
    $preflightResult.stdout | Set-Content -Path $validationPath -Encoding UTF8
}

$stageFiles = @(Get-ChildItem -Path $stageDirectory -Recurse -File)
$metadata = [pscustomobject][ordered]@{
    generatedAt = (Get-Date).ToString("o")
    packageName = $packageName
    version = $normalizedVersion
    versionTag = $versionTag
    commit = $gitCommit
    preset = $Preset
    configuration = $configuration
    stageDirectory = $stageDirectory
    zipPath = $zipPath
    validationPath = $validationPath
    bootstrapperPath = (Join-Path $stageDirectory "MasterControlBootstrapper.exe")
    serviceHostPath = (Join-Path $stageDirectory "MasterControlServiceHost.exe")
    shellPath = (Join-Path $stageDirectory "MasterControlShell.exe")
    manifestRoot = (Join-Path $stageDirectory "share\MasterControlProgram\ForsettiManifests")
    webRoot = (Join-Path $stageDirectory "share\MasterControlProgram\web")
    fileCount = $stageFiles.Count
    packageSizeBytes = ($stageFiles | Measure-Object -Property Length -Sum).Sum
    packagedPreflight = $preflightJson
}
$metadata | ConvertTo-Json -Depth 8 | Set-Content -Path $metadataPath -Encoding UTF8

if (Test-Path $zipPath) {
    Remove-Item -Path $zipPath -Force
}
Compress-Archive -Path $stageDirectory -DestinationPath $zipPath -Force

[pscustomobject][ordered]@{
    packageName = $packageName
    version = $normalizedVersion
    commit = $gitCommit
    stageDirectory = $stageDirectory
    zipPath = $zipPath
    validationPath = $validationPath
    bootstrapperPath = (Join-Path $stageDirectory "MasterControlBootstrapper.exe")
    packageMetadataPath = $metadataPath
    installInstructionsPath = $instructionsPath
} | ConvertTo-Json -Depth 6
