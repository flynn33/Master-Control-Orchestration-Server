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

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$configuration = if ($Preset -eq "debug") { "Debug" } else { "Release" }
$binaryDir = Join-Path $repoRoot ("build\" + $Preset)
. (Join-Path $PSScriptRoot "Resolve-MasterControlToolchain.ps1")

function Enter-McDevShell {
    # Use VS's own Launch-VsDevShell.ps1 to set up the developer environment
    # inside the current PowerShell session. This handles vswhere discovery,
    # MSBuild location, and Windows SDK paths natively.
    $launchScript = Join-Path (Split-Path -Parent $script:vsDevCmd) 'Launch-VsDevShell.ps1'
    if (-not (Test-Path $launchScript)) {
        throw "Could not find Launch-VsDevShell.ps1 near VsDevCmd.bat at $script:vsDevCmd."
    }

    & $launchScript -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null

    # Ensure the Windows 10 SDK bin (containing mdmerge.exe) is on PATH.
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

    $env:VCPKG_ROOT = $script:vcpkgRoot
    Set-Location $script:repoRoot
}

function Invoke-DevShell {
    param([string[]]$Commands)

    Enter-McDevShell

    foreach ($cmd in $Commands) {
        # Use cmd /c so shell-style command strings parse the way a developer
        # prompt would. The child cmd inherits $env:PATH from this
        # PowerShell session, which Launch-VsDevShell has already populated.
        cmd /c $cmd
        if ($LASTEXITCODE -ne 0) {
            throw "Command failed with exit code $LASTEXITCODE"
        }
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

$toolchain = Resolve-MasterControlToolchain
$vsDevCmd = $toolchain.VsDevCmd
$cmake = $toolchain.CMake
$ctest = $toolchain.CTest
$vcpkgRoot = $toolchain.VcpkgRoot
$vcRuntimeDirectory = $toolchain.VcRuntimeDirectory

if ([string]::IsNullOrWhiteSpace($Version)) {
    $versionDocument = Get-Content (Join-Path $repoRoot "VERSION.json") -Raw | ConvertFrom-Json
    $Version = $versionDocument.current_version
} else {
    $versionDocument = Get-Content (Join-Path $repoRoot "VERSION.json") -Raw | ConvertFrom-Json
}

$normalizedVersion = if ($Version.StartsWith("v")) { $Version.Substring(1) } else { $Version }
$versionTag = "v$normalizedVersion"
# Git metadata is best-effort: packaging can run from a source tree that is not a
# git repository (e.g. extracted from a zip or worked on without git). A null return
# from the git call used to crash the script via .Trim() on $null — now degrade
# gracefully so release packages still build outside a repo.
$gitCommitFull = ""
$gitCommit = ""
try {
    $gitRawFull = git -C $repoRoot rev-parse HEAD 2>$null
    $gitRawShort = git -C $repoRoot rev-parse --short HEAD 2>$null
    if ($LASTEXITCODE -eq 0) {
        if ($null -ne $gitRawFull) { $gitCommitFull = [string]$gitRawFull.Trim() }
        if ($null -ne $gitRawShort) { $gitCommit = [string]$gitRawShort.Trim() }
    }
} catch {
    # git not available or not a repo — leave commit fields empty.
}
if ([string]::IsNullOrWhiteSpace($gitCommit)) {
    $gitCommit = "non-git"
    $gitCommitFull = "non-git"
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot ("dist\packages\" + $Preset)
}

$packageName = "MasterControlOrchestrationServer-$versionTag-win-x64"
if ($Preset -ne "release") {
    $packageName += "-$Preset"
}

$stageDirectory = Join-Path $OutputRoot $packageName
$zipPath = Join-Path $OutputRoot ($packageName + ".zip")
$msiPath = Join-Path $OutputRoot ($packageName + ".msi")
$validationPath = Join-Path $OutputRoot ($packageName + ".preflight.json")
$metadataPath = Join-Path $stageDirectory "PACKAGE-METADATA.json"
$instructionsPath = Join-Path $stageDirectory "INSTALL.txt"
$startHerePath = Join-Path $stageDirectory "START-HERE.txt"
$installLauncherPath = Join-Path $stageDirectory "Install-MasterControlOrchestrationServer.ps1"
$setupPath = Join-Path $stageDirectory "MasterControlOrchestrationServerSetup.exe"
# PrimaryInstallerPath points at the MSI (the user-facing installer). The
# legacy .exe shim is no longer produced — see the skipped Copy-Item below.
$primaryInstallerPath = $msiPath
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

# End-user packages should not carry debug symbols. Keep the stage lean so
# the zip/MSI contain only runtime artifacts; publish symbols separately in CI.
$symbolFiles = @(Get-ChildItem -Path $stageDirectory -Recurse -Filter *.pdb -File)
foreach ($symbolFile in $symbolFiles) {
    Remove-Item -LiteralPath $symbolFile.FullName -Force
}

$vcRuntimeFiles = @(Get-ChildItem -Path $vcRuntimeDirectory -Filter *.dll -File | Sort-Object Name)
foreach ($vcRuntimeFile in $vcRuntimeFiles) {
    Copy-Item -Path $vcRuntimeFile.FullName -Destination (Join-Path $stageDirectory $vcRuntimeFile.Name) -Force
}

# The user-friendly "Install Master Control Orchestration Server.exe" shim is
# deliberately NOT produced any more. As of v0.4.3-rc.1 the WiX MSI is the
# primary install artifact — the legacy Tron-cyan progress-window launcher
# is kept as MasterControlOrchestrationServerSetup.exe for advanced/CI
# headless rollouts only, and the MSI drives deferred custom actions against
# MasterControlBootstrapper.exe directly. Having both installers side-by-side
# confused operators into double-clicking the old .exe and getting the old
# experience; removing the shim makes the MSI the only obvious user path.

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

$msiFileName = Split-Path -Leaf $msiPath
$instructions = @"
Master Control Orchestration Server $versionTag

Package root: $packageName
Build configuration: $configuration
Commit: $gitCommit

Quick start (interactive install)
Double-click $msiFileName. That opens the Windows Installer with a native
wizard: Welcome -> License -> Install Location -> Options (service /
firewall / Start Menu shortcut / Desktop shortcut / launch-on-finish) ->
Install. No PowerShell needed. UAC will prompt for admin because the
service host runs at the machine scope.

Silent / CI install
msiexec /i $msiFileName /qn /l*v install.log

Headless (no MSI) install via the CLI bootstrapper
.\MasterControlBootstrapper.exe preflight "C:\Program Files\Master Control Orchestration Server" --json
.\MasterControlBootstrapper.exe install "C:\Program Files\Master Control Orchestration Server" --json

Per-user install (non-admin test, no service/firewall changes)
.\MasterControlBootstrapper.exe install "$env:LOCALAPPDATA\MasterControlOrchestrationServer" --skip-service --skip-firewall --skip-uninstall-registration --json

Validation
.\MasterControlBootstrapper.exe validate "C:\Program Files\Master Control Orchestration Server" --json

Uninstall (MSI-installed)
msiexec /x $msiFileName /qn
"@
Set-Content -Path $instructionsPath -Value $instructions -Encoding UTF8

$startHere = @"
MASTER CONTROL ORCHESTRATION SERVER

START HERE

Double-click $msiFileName.

That opens the Windows Installer wizard (Welcome -> License ->
Install Location -> Options -> Install). UAC will prompt for admin
because the service host runs machine-scope. No PowerShell needed.

Advanced (CI / headless / power users):
.\INSTALL.txt has the full msiexec and MasterControlBootstrapper.exe
command-line options.

USING THE SERVER ON THE HOST MACHINE

The Master Control Orchestration Server runs as a Windows service and ships
with a native desktop shell (MasterControlShell.exe). On the machine where
you installed the server, launch the desktop shell — it is the full
Windows-application surface for operator use and lives in the Start Menu
under 'Master Control Orchestration Server'.

The browser dashboard is intended for remote clients on the LAN. A
shortcut to it is installed under 'Start Menu > Master Control
Orchestration Server > Remote Access > Browser Dashboard (Remote)' so it
does not compete with the desktop shell on the host itself.
"@
Set-Content -Path $startHerePath -Value $startHere -Encoding UTF8

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
    versionTrackingBaseCommit = $versionDocument.last_release_commit
    preset = $Preset
    configuration = $configuration
    stageDirectory = $stageDirectory
    zipPath = $zipPath
    validationPath = $validationPath
    bootstrapperPath = (Join-Path $stageDirectory "MasterControlBootstrapper.exe")
    primaryInstallerPath = $primaryInstallerPath
    setupPath = $setupPath
    installLauncherPath = $installLauncherPath
    startHerePath = $startHerePath
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

# ---------------------------------------------------------------------------
# Build the Windows Installer (MSI). This is the primary user-facing install
# artifact. Failing to build it is not fatal — the zip still ships for
# headless / scripted rollouts — but we warn so CI catches the regression.
# ---------------------------------------------------------------------------
$msiBuildScript = Join-Path $repoRoot "installer\Build-Msi.ps1"
$msiBuildResult = $null
if (Test-Path $msiBuildScript) {
    try {
        if (Test-Path $msiPath) { Remove-Item -Path $msiPath -Force }
        $rawMsiBuildResult = & $msiBuildScript `
            -StageDirectory $stageDirectory `
            -Version $normalizedVersion `
            -OutputMsiPath $msiPath `
            -IconsDir (Join-Path $repoRoot "resources\icons") `
            -PackagingDir (Join-Path $repoRoot "resources\icons\packaging") `
            -InstallerDir (Join-Path $repoRoot "installer")
        $msiBuildResult = @($rawMsiBuildResult) |
            Where-Object {
                $_ -is [psobject] -and
                $_.PSObject.Properties.Name -contains "MsiVersion"
            } |
            Select-Object -Last 1
        if ($null -eq $msiBuildResult) {
            throw "Build-Msi.ps1 did not return MSI metadata."
        }
        $metadata | Add-Member -NotePropertyName msiPath -NotePropertyValue $msiPath -Force
        $metadata | Add-Member -NotePropertyName msiVersion -NotePropertyValue $msiBuildResult.MsiVersion -Force
        $metadata | ConvertTo-Json -Depth 8 | Set-Content -Path $metadataPath -Encoding UTF8
    } catch {
        Write-Warning "MSI build failed: $($_.Exception.Message). Continuing with zip-only output."
    }
} else {
    Write-Warning "installer\Build-Msi.ps1 not found. Skipping MSI build."
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
    primaryInstallerPath = $primaryInstallerPath
    setupPath = $setupPath
    packageMetadataPath = $metadataPath
    installInstructionsPath = $instructionsPath
    installLauncherPath = $installLauncherPath
    startHerePath = $startHerePath
    vcRuntimeDirectory = $vcRuntimeDirectory
    vcRuntimeFiles = @($vcRuntimeFiles | ForEach-Object { $_.Name })
    readinessJsonPath = if ($metadata.PSObject.Properties.Name -contains "readinessJsonPath") { $metadata.readinessJsonPath } else { "" }
    readinessSummaryPath = if ($metadata.PSObject.Properties.Name -contains "readinessSummaryPath") { $metadata.readinessSummaryPath } else { "" }
} | ConvertTo-Json -Depth 6
