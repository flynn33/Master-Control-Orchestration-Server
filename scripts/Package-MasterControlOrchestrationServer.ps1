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

function ConvertTo-ExtendedLengthPath {
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if ($fullPath.StartsWith("\\?\")) {
        return $fullPath
    }

    if ($fullPath.StartsWith("\\\\")) {
        return "\\?\UNC\" + $fullPath.TrimStart('\')
    }

    return "\\?\" + $fullPath
}

function Remove-DirectoryRobust {
    param(
        [Parameter(Mandatory)]
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }

    try {
        Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction Stop
    } catch {
        if (-not (Test-Path -LiteralPath $Path)) {
            return
        }

        $extendedPath = ConvertTo-ExtendedLengthPath -Path $Path
        if ([System.IO.Directory]::Exists($extendedPath)) {
            [System.IO.Directory]::Delete($extendedPath, $true)
        }
    }

    if (Test-Path -LiteralPath $Path) {
        throw "Failed to remove directory '$Path'."
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

# Keep the install payload under a short internal build path. WiX still hits
# MAX_PATH limits on staged file references even when PowerShell can see them
# via extended-length paths, so the caller's OutputRoot should not determine
# the payload path depth.
$payloadRoot = Join-Path $repoRoot "build\_payload"
$stageDirectory = Join-Path $payloadRoot $packageName
$bundleDirectory = Join-Path $OutputRoot $packageName
$zipPath = Join-Path $OutputRoot ($packageName + ".zip")
$msiPath = Join-Path $OutputRoot ($packageName + ".msi")
$validationPath = Join-Path $OutputRoot ($packageName + ".preflight.json")
$bundleValidationPath = Join-Path $bundleDirectory (Split-Path -Leaf $validationPath)
$metadataPath = Join-Path $bundleDirectory "PACKAGE-METADATA.json"
$instructionsPath = Join-Path $bundleDirectory "INSTALL.txt"
$startHerePath = Join-Path $bundleDirectory "START-HERE.txt"
$installLauncherPath = ""
$setupPath = ""
$bundleMsiPath = Join-Path $bundleDirectory (Split-Path -Leaf $msiPath)
# PrimaryInstallerPath points at the MSI (the user-facing installer). The
# legacy .exe shim is no longer produced — see the skipped Copy-Item below.
$primaryInstallerPath = $bundleMsiPath
$validationTarget = Join-Path $OutputRoot ($packageName + ".validation-target")

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
New-Item -ItemType Directory -Force -Path $payloadRoot | Out-Null
if ((Test-Path $stageDirectory) -and -not $KeepStage) {
    Remove-DirectoryRobust -Path $stageDirectory
}
if (Test-Path $bundleDirectory) {
    Remove-DirectoryRobust -Path $bundleDirectory
}
if (Test-Path $validationTarget) {
    Remove-DirectoryRobust -Path $validationTarget
}
New-Item -ItemType Directory -Force -Path $bundleDirectory | Out-Null

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

# The MSI is the only supported interactive installer shipped to operators.
# Keep the legacy bootstrapper payload internal to the stage directory for MSI
# custom actions and validation only; do not generate or advertise an
# additional end-user launcher script/exe.

$msiFileName = Split-Path -Leaf $msiPath
$instructions = @"
Master Control Orchestration Server $versionTag

Bundle root: $packageName
Build configuration: $configuration
Commit: $gitCommit

Interactive install
Double-click $msiFileName. This is the supported end-user installer.
It opens the native Windows Installer wizard with install location plus
service, firewall, Start Menu shortcut, Desktop shortcut, and
launch-on-finish options. No PowerShell is required.

Silent / CI install
msiexec /i $msiFileName /qn /l*v install.log

Host machine use
On the machine where the server is installed, launch the Master Control
Orchestration Server Windows app from the Start Menu or Desktop shortcut.
It is the primary operator surface on the host machine. The browser
dashboard is for remote access from other clients on the LAN.

Validation
$(Split-Path -Leaf $bundleValidationPath)

Uninstall (MSI-installed)
msiexec /x $msiFileName /qn
"@
Set-Content -Path $instructionsPath -Value $instructions -Encoding UTF8

$startHere = @"
MASTER CONTROL ORCHESTRATION SERVER

START HERE

Double-click $msiFileName.

That opens the Windows Installer wizard (Welcome -> License ->
Install Location -> Options -> Install). No PowerShell needed.

The MSI is the supported installer for operators. Use it if you want the
normal Windows setup flow with Start Menu / Desktop shortcut options.

Advanced / CI:
.\INSTALL.txt has the full msiexec command-line options.

USING THE SERVER ON THE HOST MACHINE

The Master Control Orchestration Server runs as a Windows service and ships
with a native Windows application. On the machine where you installed the
server, launch the Windows app - it is the full Windows-application surface
for operator use and lives in the Start Menu under
'Master Control Orchestration Server'.

The browser dashboard is intended for remote clients on the LAN. A
shortcut to it is installed under 'Start Menu > Master Control
Orchestration Server > Remote Access > Browser Dashboard (Remote)' so it
does not compete with the Windows app on the host itself.
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
    Copy-Item -Path $validationPath -Destination $bundleValidationPath -Force
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
    bundleDirectory = $bundleDirectory
    zipPath = $zipPath
    validationPath = $bundleValidationPath
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
    $stageReadinessJsonPath = Join-Path $bundleDirectory "RELEASE-READINESS.json"
    $stageReadinessSummaryPath = Join-Path $bundleDirectory "RELEASE-READINESS.md"

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
        Copy-Item -Path $msiPath -Destination $bundleMsiPath -Force
        $metadata | Add-Member -NotePropertyName msiPath -NotePropertyValue $bundleMsiPath -Force
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
Compress-Archive -Path $bundleDirectory -DestinationPath $zipPath -Force

[pscustomobject][ordered]@{
    packageName = $packageName
    version = $normalizedVersion
    commit = $gitCommit
    stageDirectory = $stageDirectory
    bundleDirectory = $bundleDirectory
    zipPath = $zipPath
    validationPath = $bundleValidationPath
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
