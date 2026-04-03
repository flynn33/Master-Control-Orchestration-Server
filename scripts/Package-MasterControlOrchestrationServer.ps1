# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

[CmdletBinding()]
param(
    [ValidateSet("debug", "release")]
    [string]$Preset = "release",
    [string]$Version = "",
    [string]$OutputRoot = "",
    [string]$AcceptanceReportPath = "",
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

function Resolve-VcRuntimeDirectory {
    $redistRoots = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC"
    )

    foreach ($root in $redistRoots) {
        if (-not (Test-Path $root)) {
            continue
        }

        $candidates = @(Get-ChildItem -Path $root -Directory | Sort-Object Name -Descending)
        foreach ($candidate in $candidates) {
            $crtDirectory = Join-Path $candidate.FullName "x64\Microsoft.VC143.CRT"
            if (Test-Path $crtDirectory) {
                return $crtDirectory
            }
        }
    }

    throw "Microsoft VC++ x64 runtime directory was not found."
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
$vcRuntimeDirectory = Resolve-VcRuntimeDirectory

if ([string]::IsNullOrWhiteSpace($Version)) {
    $versionDocument = Get-Content (Join-Path $repoRoot "VERSION.json") -Raw | ConvertFrom-Json
    $Version = $versionDocument.current_version
} else {
    $versionDocument = Get-Content (Join-Path $repoRoot "VERSION.json") -Raw | ConvertFrom-Json
}

$normalizedVersion = if ($Version.StartsWith("v")) { $Version.Substring(1) } else { $Version }
$versionTag = "v$normalizedVersion"
$gitCommitFull = (git -C $repoRoot rev-parse HEAD).Trim()
$gitCommit = (git -C $repoRoot rev-parse --short HEAD).Trim()

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot ("dist\packages\" + $Preset)
}

$packageName = "MasterControlOrchestrationServer-$versionTag-win-x64"
if ($Preset -ne "release") {
    $packageName += "-$Preset"
}

$stageDirectory = Join-Path $OutputRoot $packageName
$zipPath = Join-Path $OutputRoot ($packageName + ".zip")
$validationPath = Join-Path $OutputRoot ($packageName + ".preflight.json")
$metadataPath = Join-Path $stageDirectory "PACKAGE-METADATA.json"
$instructionsPath = Join-Path $stageDirectory "INSTALL.txt"
$installLauncherPath = Join-Path $stageDirectory "Install-MasterControlOrchestrationServer.ps1"
$setupPath = Join-Path $stageDirectory "MasterControlOrchestrationServerSetup.exe"
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

$vcRuntimeFiles = @(Get-ChildItem -Path $vcRuntimeDirectory -Filter *.dll -File | Sort-Object Name)
foreach ($vcRuntimeFile in $vcRuntimeFiles) {
    Copy-Item -Path $vcRuntimeFile.FullName -Destination (Join-Path $stageDirectory $vcRuntimeFile.Name) -Force
}

$installLauncher = @'
param(
    [string]$InstallDirectory = "C:\Program Files\Master Control Orchestration Server",
    [switch]$SkipService,
    [switch]$SkipFirewall,
    [switch]$SkipShortcuts,
    [switch]$SkipUninstallRegistration,
    [switch]$ElevatedRelay
)

$ErrorActionPreference = "Continue"
$desktopDirectory = if (-not [string]::IsNullOrWhiteSpace($env:MASTERCONTROL_BOOTSTRAPPER_LOG_DIR)) {
    $env:MASTERCONTROL_BOOTSTRAPPER_LOG_DIR
} else {
    [Environment]::GetFolderPath("Desktop")
}
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss-fff"
$logPath = Join-Path $desktopDirectory ("MasterControlOrchestrationServer-install-launcher-" + $timestamp + ".txt")
$bootstrapperPath = Join-Path $PSScriptRoot "MasterControlBootstrapper.exe"
$arguments = @("install", $InstallDirectory, "--json")
if ($SkipService) { $arguments += "--skip-service" }
if ($SkipFirewall) { $arguments += "--skip-firewall" }
if ($SkipShortcuts) { $arguments += "--skip-shortcuts" }
if ($SkipUninstallRegistration) { $arguments += "--skip-uninstall-registration" }

function Test-IsAdministrator {
    try {
        $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
        $principal = [Security.Principal.WindowsPrincipal]::new($identity)
        return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    } catch {
        return $false
    }
}

function Test-PathRequiresElevation {
    param([string]$Path)

    try {
        $candidate = [System.IO.Path]::GetFullPath($Path)
    } catch {
        $candidate = $Path
    }

    $roots = @(
        [Environment]::GetFolderPath("ProgramFiles"),
        [Environment]::GetFolderPath("ProgramFilesX86"),
        $env:ProgramW6432,
        $env:ProgramFiles,
        [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique

    foreach ($root in $roots) {
        try {
            $normalizedRoot = [System.IO.Path]::GetFullPath($root).TrimEnd('\') + '\'
            $normalizedCandidate = $candidate.TrimEnd('\') + '\'
            if ($normalizedCandidate.StartsWith($normalizedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
                return $true
            }
        } catch {
        }
    }

    return $false
}

function ConvertTo-PowerShellLiteralArgument {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Value
    )

    return "'" + ($Value -replace "'", "''") + "'"
}

function Write-LauncherLog {
    param(
        [string]$Result,
        [int]$ExitCode,
        [bool]$IsAdministrator,
        [bool]$RequiresElevation,
        [bool]$ElevationAttempted,
        [bool]$ProgramFilesProtectedTarget,
        [bool]$ManagedIntegrationsRequested
    )

    New-Item -ItemType Directory -Force -Path $desktopDirectory | Out-Null
    @(
        "Master Control Orchestration Server Install Launcher",
        "",
        "GeneratedAt: $(Get-Date -Format o)",
        "BootstrapperPath: $bootstrapperPath",
        "InstallDirectory: $InstallDirectory",
        "IsAdministrator: $IsAdministrator",
        "RequiresElevation: $RequiresElevation",
        "ElevationAttempted: $ElevationAttempted",
        "ManagedIntegrationsRequested: $ManagedIntegrationsRequested",
        "ProgramFilesProtectedTarget: $ProgramFilesProtectedTarget",
        "ElevatedRelay: $ElevatedRelay",
        "ExitCode: $ExitCode",
        "",
        "Output",
        "------",
        ($Result.TrimEnd())
    ) | Set-Content -Path $logPath -Encoding UTF8
}

$isAdministrator = Test-IsAdministrator
$programFilesProtectedTarget = Test-PathRequiresElevation -Path $InstallDirectory
$managedIntegrationsRequested = (-not $SkipService) -or (-not $SkipFirewall) -or (-not $SkipUninstallRegistration)
$requiresElevation = $programFilesProtectedTarget -or $managedIntegrationsRequested
$elevationAttempted = $false

try {
    if ($requiresElevation -and -not $isAdministrator -and -not $ElevatedRelay) {
        $elevationAttempted = $true
        $relayCommand = @(
            "&",
            (ConvertTo-PowerShellLiteralArgument -Value $PSCommandPath),
            "-InstallDirectory",
            (ConvertTo-PowerShellLiteralArgument -Value $InstallDirectory),
            "-ElevatedRelay"
        )

        if ($SkipService) { $relayCommand += "-SkipService" }
        if ($SkipFirewall) { $relayCommand += "-SkipFirewall" }
        if ($SkipShortcuts) { $relayCommand += "-SkipShortcuts" }
        if ($SkipUninstallRegistration) { $relayCommand += "-SkipUninstallRegistration" }

        $encodedCommand = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes(($relayCommand -join " ")))
        $process = Start-Process -FilePath "powershell.exe" -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-EncodedCommand", $encodedCommand) -Verb RunAs -Wait -PassThru
        $exitCode = $process.ExitCode
        $result = @"
Launcher detected that this install requires elevation and delegated to an elevated PowerShell process.
ChildExitCode: $exitCode
If the elevated process wrote its own launcher/bootstrapper log, check the desktop for the newer file.
"@
    } else {
        $result = & $bootstrapperPath @arguments 2>&1 | Out-String
        $exitCode = $LASTEXITCODE
    }
} catch {
    $result = if ($null -ne $_.Exception) { $_.Exception.ToString() } else { $_.ToString() }
    $exitCode = 1
}

Write-LauncherLog `
    -Result $result `
    -ExitCode $exitCode `
    -IsAdministrator $isAdministrator `
    -RequiresElevation $requiresElevation `
    -ElevationAttempted $elevationAttempted `
    -ProgramFilesProtectedTarget $programFilesProtectedTarget `
    -ManagedIntegrationsRequested $managedIntegrationsRequested

Write-Host "Install launcher log written to $logPath"
if (-not [string]::IsNullOrWhiteSpace($result)) {
    Write-Output $result.TrimEnd()
}

exit $exitCode
'@
Set-Content -Path $installLauncherPath -Value $installLauncher -Encoding UTF8

$instructions = @"
Master Control Orchestration Server $versionTag

Package root: $packageName
Build configuration: $configuration
Commit: $gitCommit

Quick start
1. Extract this package to a writable local folder.
2. Keep the bundled VC++ runtime DLL files beside the executables.
3. Prefer .\MasterControlOrchestrationServerSetup.exe for the standard interactive install experience.
4. Use .\Install-MasterControlOrchestrationServer.ps1 as the diagnostic fallback because it always writes a desktop log and will request elevation for the default managed install path.
5. Open PowerShell only when you need the bootstrapper or fallback launcher directly.
6. Run a preflight check before installing.
7. If included, review RELEASE-READINESS.md before target-host deployment validation.

Fully managed install (requires Administrator)
.\MasterControlOrchestrationServerSetup.exe
.\Install-MasterControlOrchestrationServer.ps1 -InstallDirectory "C:\Program Files\Master Control Orchestration Server"
.\MasterControlBootstrapper.exe preflight "C:\Program Files\Master Control Orchestration Server" --json
.\MasterControlBootstrapper.exe install "C:\Program Files\Master Control Orchestration Server" --json

Non-admin test install
.\MasterControlOrchestrationServerSetup.exe --install-directory "$env:LOCALAPPDATA\MasterControlOrchestrationServer" --skip-service --skip-firewall --skip-uninstall-registration --quiet
.\Install-MasterControlOrchestrationServer.ps1 -InstallDirectory "$env:LOCALAPPDATA\MasterControlOrchestrationServer" -SkipService -SkipFirewall -SkipUninstallRegistration
.\MasterControlBootstrapper.exe preflight "$env:LOCALAPPDATA\MasterControlOrchestrationServer" --skip-service --skip-firewall --skip-uninstall-registration --json
.\MasterControlBootstrapper.exe install "$env:LOCALAPPDATA\MasterControlOrchestrationServer" --skip-service --skip-firewall --skip-uninstall-registration --json

Validation
.\MasterControlBootstrapper.exe validate "C:\Program Files\Master Control Orchestration Server" --json

Uninstall
.\MasterControlBootstrapper.exe uninstall "C:\Program Files\Master Control Orchestration Server" --json
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
    commitFull = $gitCommitFull
    trackedReleaseCommit = $versionDocument.last_release_commit
    preset = $Preset
    configuration = $configuration
    stageDirectory = $stageDirectory
    zipPath = $zipPath
    validationPath = $validationPath
    bootstrapperPath = (Join-Path $stageDirectory "MasterControlBootstrapper.exe")
    setupPath = $setupPath
    installLauncherPath = $installLauncherPath
    serviceHostPath = (Join-Path $stageDirectory "MasterControlServiceHost.exe")
    shellPath = (Join-Path $stageDirectory "MasterControlShell.exe")
    manifestRoot = (Join-Path $stageDirectory "share\MasterControlOrchestrationServer\ForsettiManifests")
    webRoot = (Join-Path $stageDirectory "share\MasterControlOrchestrationServer\web")
    vcRuntimeDirectory = $vcRuntimeDirectory
    vcRuntimeBundled = ($vcRuntimeFiles.Count -gt 0)
    vcRuntimeFiles = @($vcRuntimeFiles | ForEach-Object { $_.Name })
    fileCount = $stageFiles.Count
    packageSizeBytes = ($stageFiles | Measure-Object -Property Length -Sum).Sum
    packagedPreflight = $preflightJson
}
$metadata | ConvertTo-Json -Depth 8 | Set-Content -Path $metadataPath -Encoding UTF8

if (-not [string]::IsNullOrWhiteSpace($AcceptanceReportPath)) {
    $readinessScriptPath = Join-Path $PSScriptRoot "Get-MasterControlOrchestrationServerReleaseReadiness.ps1"
    $stageReadinessJsonPath = Join-Path $stageDirectory "RELEASE-READINESS.json"
    $stageReadinessSummaryPath = Join-Path $stageDirectory "RELEASE-READINESS.md"

    $readinessResult = Invoke-CapturedProcess -FilePath "powershell.exe" -Arguments @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $readinessScriptPath,
        "-PackageMetadataPath", $metadataPath,
        "-AcceptanceReportPath", $AcceptanceReportPath,
        "-OutputPath", $stageReadinessJsonPath,
        "-SummaryPath", $stageReadinessSummaryPath
    )

    if ($readinessResult.exitCode -ne 0) {
        throw "Packaged release readiness generation failed: $($readinessResult.stderr)$($readinessResult.stdout)"
    }

    $metadata | Add-Member -NotePropertyName readinessJsonPath -NotePropertyValue $stageReadinessJsonPath
    $metadata | Add-Member -NotePropertyName readinessSummaryPath -NotePropertyValue $stageReadinessSummaryPath
    $metadata | ConvertTo-Json -Depth 8 | Set-Content -Path $metadataPath -Encoding UTF8
}

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
    setupPath = $setupPath
    packageMetadataPath = $metadataPath
    installInstructionsPath = $instructionsPath
    installLauncherPath = $installLauncherPath
    vcRuntimeDirectory = $vcRuntimeDirectory
    vcRuntimeFiles = @($vcRuntimeFiles | ForEach-Object { $_.Name })
    readinessJsonPath = if ($metadata.PSObject.Properties.Name -contains "readinessJsonPath") { $metadata.readinessJsonPath } else { "" }
    readinessSummaryPath = if ($metadata.PSObject.Properties.Name -contains "readinessSummaryPath") { $metadata.readinessSummaryPath } else { "" }
} | ConvertTo-Json -Depth 6
