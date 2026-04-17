# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

[CmdletBinding()]
param(
    [string]$PackageMetadataPath = "",
    [string]$AcceptanceReportPath = "",
    [string]$OutputPath = "",
    [string]$SummaryPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

function Read-JsonDocument {
    param([string]$Path)

    $resolvedPath = (Resolve-Path $Path).Path
    return (Get-Content $resolvedPath -Raw | ConvertFrom-Json)
}

function Resolve-LatestPackageMetadataPath {
    $metadataFiles = @(Get-ChildItem -Path (Join-Path $repoRoot "dist\packages\release") -Recurse -Filter "PACKAGE-METADATA.json" -File -ErrorAction SilentlyContinue)
    if ($metadataFiles.Count -eq 0) {
        throw "No package metadata files were found under dist\packages\release."
    }

    return ($metadataFiles | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
}

function Resolve-AcceptanceReportPath {
    param([string]$VersionTag)

    $preferredPath = Join-Path $repoRoot ("build\release\deployment-acceptance-package-release-" + $VersionTag + ".json")
    if (Test-Path $preferredPath) {
        return (Resolve-Path $preferredPath).Path
    }

    $reports = @(Get-ChildItem -Path (Join-Path $repoRoot "build\release") -Filter "deployment-acceptance-package-release*.json" -File -ErrorAction SilentlyContinue |
        Where-Object { $_.BaseName -notlike "*comparison*" } |
        Sort-Object LastWriteTime -Descending)
    if ($reports.Count -eq 0) {
        throw "No deployment acceptance reports were found under build\release."
    }

    return $reports[0].FullName
}

function New-ReadinessCheck {
    param(
        [string]$Name,
        [bool]$Passed,
        [string]$Message
    )

    return [pscustomobject][ordered]@{
        name = $Name
        passed = $Passed
        message = $Message
    }
}

function Get-SignificantStatusEntries {
    param([string[]]$StatusLines)

    $entries = New-Object System.Collections.Generic.List[string]
    foreach ($line in $StatusLines) {
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        $path = if ($line.Length -gt 3) { $line.Substring(3).Trim() } else { "" }
        if ($path -like "docs/handoff*") {
            continue
        }

        $entries.Add($line)
    }

    return $entries
}

function New-ReleaseReadinessSummary {
    param([object]$Report)

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# Master Control Release Readiness")
    $lines.Add("")
    $lines.Add("* Generated: $($Report.generatedAt)")
    $lines.Add("* Status: $($Report.releaseStatus)")
    $lines.Add("* Deployment gate: $($Report.deploymentGate)")
    $lines.Add("* Branch: $($Report.repository.branch)")
    $lines.Add("* Head commit: $($Report.repository.headShort)")
    $lines.Add("* Package version: $($Report.package.versionTag)")
    $lines.Add("* Package commit: $($Report.package.commit)")
    $lines.Add("* Package zip: $($Report.package.zipPath)")
    $lines.Add("* Acceptance report: $($Report.acceptance.reportPath)")
    $lines.Add("")

    $lines.Add("## Checks")
    $lines.Add("")
    foreach ($check in $Report.checks) {
        $marker = if ($check.passed) { "[pass]" } else { "[fail]" }
        $lines.Add("* $marker $($check.name): $($check.message)")
    }
    $lines.Add("")

    $lines.Add("## Package")
    $lines.Add("")
    $lines.Add("* Bootstrapper: $($Report.package.bootstrapperPath)")
    $lines.Add("* Service host: $($Report.package.serviceHostPath)")
    $lines.Add("* Shell: $($Report.package.shellPath)")
    $lines.Add("* Manifest root: $($Report.package.manifestRoot)")
    $lines.Add("* Web root: $($Report.package.webRoot)")
    $lines.Add("* File count: $($Report.package.fileCount)")
    $lines.Add("* Size bytes: $($Report.package.packageSizeBytes)")
    $lines.Add("* Packaged preflight ready: $($Report.package.packagedPreflight.ready)")
    $lines.Add("")

    $lines.Add("## Local Acceptance")
    $lines.Add("")
    $lines.Add("* Host label: $($Report.acceptance.hostLabel)")
    $lines.Add("* Host: $($Report.acceptance.hostCaption) ($($Report.acceptance.hostVersion) build $($Report.acceptance.hostBuildNumber))")
    $lines.Add("* Scenario: $($Report.acceptance.scenario)")
    $lines.Add("* Result: $(if ($Report.acceptance.succeeded) { 'passed' } else { 'failed' })")
    $lines.Add("* Administrator: $($Report.acceptance.isAdministrator)")
    $lines.Add("* Managed mutating: $($Report.acceptance.managedMutating)")
    $lines.Add("* Bundle: $($Report.acceptance.bundlePath)")
    if (-not [string]::IsNullOrWhiteSpace($Report.acceptance.hostDiagnosticsDirectory)) {
        $lines.Add("* Host diagnostics: $($Report.acceptance.hostDiagnosticsDirectory)")
    }
    $lines.Add("")

    $lines.Add("## Remaining External Validation")
    $lines.Add("")
    if ($Report.remainingExternalValidation.Count -eq 0) {
        $lines.Add("* None. Current evidence covers the required deployment lanes.")
    } else {
        foreach ($item in $Report.remainingExternalValidation) {
            $lines.Add("* $item")
        }
    }
    $lines.Add("")

    $lines.Add("## Recommended Commands")
    $lines.Add("")
    foreach ($command in $Report.recommendedCommands) {
        $lines.Add("* $($command.label)")
        $lines.Add("")
        $lines.Add('```powershell')
        $lines.Add($command.command)
        $lines.Add('```')
        $lines.Add("")
    }

    $lines.Add("## Notes")
    $lines.Add("")
    foreach ($note in $Report.notes) {
        $lines.Add("* $($note)")
    }

    return [string]::Join([Environment]::NewLine, $lines)
}

if ([string]::IsNullOrWhiteSpace($PackageMetadataPath)) {
    $PackageMetadataPath = Resolve-LatestPackageMetadataPath
}

$packageMetadata = Read-JsonDocument -Path $PackageMetadataPath

if ([string]::IsNullOrWhiteSpace($AcceptanceReportPath)) {
    $AcceptanceReportPath = Resolve-AcceptanceReportPath -VersionTag $packageMetadata.versionTag
}

$acceptanceReport = Read-JsonDocument -Path $AcceptanceReportPath
$versionDocument = Read-JsonDocument -Path (Join-Path $repoRoot "VERSION.json")

$packageVersionTrackingBaseCommit = ""
if ($packageMetadata.PSObject.Properties.Name -contains "versionTrackingBaseCommit") {
    $packageVersionTrackingBaseCommit = [string]$packageMetadata.versionTrackingBaseCommit
} elseif ($packageMetadata.PSObject.Properties.Name -contains "releaseSourceCommit") {
    $packageVersionTrackingBaseCommit = [string]$packageMetadata.releaseSourceCommit
} elseif ($packageMetadata.PSObject.Properties.Name -contains "trackedReleaseCommit") {
    $packageVersionTrackingBaseCommit = [string]$packageMetadata.trackedReleaseCommit
}

$headFull = (git -C $repoRoot rev-parse HEAD).Trim()
$headShort = (git -C $repoRoot rev-parse --short HEAD).Trim()
$branchName = (git -C $repoRoot rev-parse --abbrev-ref HEAD).Trim()
$porcelainStatus = @(git -C $repoRoot status --porcelain)
$significantStatusEntries = @(Get-SignificantStatusEntries -StatusLines $porcelainStatus)
$repositoryDirty = ($significantStatusEntries.Count -gt 0)

$aheadCount = 0
$behindCount = 0
$originRef = "origin/$branchName"
try {
    git -C $repoRoot rev-parse --verify $originRef | Out-Null
    $aheadBehind = (git -C $repoRoot rev-list --left-right --count "$originRef...HEAD").Trim() -split "\s+"
    if ($aheadBehind.Count -ge 2) {
        $behindCount = [int]$aheadBehind[0]
        $aheadCount = [int]$aheadBehind[1]
    }
} catch {
    $aheadCount = 0
    $behindCount = 0
}

$packageCommitMatchesHead = $headFull.StartsWith([string]$packageMetadata.commit) -or ($headShort -eq [string]$packageMetadata.commit)
$packagedPreflightReady = [bool]$packageMetadata.packagedPreflight.ready
$acceptanceSucceeded = [bool]$acceptanceReport.succeeded
$acceptanceOnWindows11 = ($acceptanceReport.host.caption -like "*Windows 11*")
$managedValidationCovered = ([bool]$acceptanceReport.isAdministrator -and [bool]$acceptanceReport.managedMutating) -or ($acceptanceReport.hostLabel -like "*managed*")
$server2022Covered = ($acceptanceReport.host.caption -like "*Server 2022*") -or ($acceptanceReport.hostLabel -like "*server2022*")

$checks = @(
    (New-ReadinessCheck -Name "Package metadata exists" -Passed $true -Message ("Using " + (Resolve-Path $PackageMetadataPath).Path)),
    (New-ReadinessCheck -Name "Package zip exists" -Passed (Test-Path $packageMetadata.zipPath) -Message $packageMetadata.zipPath),
    (New-ReadinessCheck -Name "Bootstrapper exists" -Passed (Test-Path $packageMetadata.bootstrapperPath) -Message $packageMetadata.bootstrapperPath),
    (New-ReadinessCheck -Name "Service host exists" -Passed (Test-Path $packageMetadata.serviceHostPath) -Message $packageMetadata.serviceHostPath),
    (New-ReadinessCheck -Name "WinUI shell exists" -Passed (Test-Path $packageMetadata.shellPath) -Message $packageMetadata.shellPath),
    (New-ReadinessCheck -Name "Packaged preflight" -Passed $packagedPreflightReady -Message "Packaged bootstrapper preflight is ready."),
    (New-ReadinessCheck -Name "Local packaged acceptance" -Passed $acceptanceSucceeded -Message ("Acceptance report " + (Resolve-Path $AcceptanceReportPath).Path)),
    (New-ReadinessCheck -Name "Package commit matches repo head" -Passed $packageCommitMatchesHead -Message ("Package commit " + $packageMetadata.commit + "; repo head " + $headShort)),
    (New-ReadinessCheck -Name "Repository clean" -Passed (-not $repositoryDirty) -Message $(if ($repositoryDirty) { "Working tree has local changes." } else { "Working tree is clean." })),
    (New-ReadinessCheck -Name "Repository sync with origin" -Passed (($aheadCount -eq 0) -and ($behindCount -eq 0)) -Message ("Ahead " + $aheadCount + ", behind " + $behindCount))
)

$remainingExternalValidation = New-Object System.Collections.Generic.List[string]
if (-not $managedValidationCovered) {
    $remainingExternalValidation.Add("Run an elevated fully managed Windows 11 acceptance pass with service, firewall, and uninstall-registration integration enabled.")
}
if (-not $server2022Covered) {
    $remainingExternalValidation.Add("Run the deployment acceptance harness on a Windows Server 2022 host and review any target-host failures.")
}

$deploymentGate = if ($remainingExternalValidation.Count -eq 0) { "ReadyForDeploymentValidation" } else { "PendingTargetHostValidation" }
$releaseStatus = if (($checks | Where-Object { -not $_.passed }).Count -eq 0 -and $acceptanceOnWindows11) {
    if ($remainingExternalValidation.Count -eq 0) { "ReadyForDeployment" } else { "ReadyForInstallerTesting" }
} else {
    "Blocked"
}

$bootstrapperPath = (Resolve-Path $packageMetadata.bootstrapperPath).Path
$testScriptPath = (Resolve-Path (Join-Path $PSScriptRoot "Test-MasterControlOrchestrationServerDeployment.ps1")).Path
$versionTag = [string]$packageMetadata.versionTag
$recommendedCommands = @(
    [pscustomobject][ordered]@{
        label = "Elevated managed Windows 11 acceptance"
        command = ('powershell.exe -NoProfile -ExecutionPolicy Bypass -File "{0}" -BootstrapperPath "{1}" -Scenario both -ManagedMutating -AutoElevateManaged -HostLabel "package-release-managed-{2}" -ReportPath "{3}"' -f
            $testScriptPath,
            $bootstrapperPath,
            $versionTag,
            (Join-Path $repoRoot ("build\release\deployment-acceptance-package-release-" + $versionTag + ".managed.json")))
    },
    [pscustomobject][ordered]@{
        label = "Windows Server 2022 acceptance"
        command = ('powershell.exe -NoProfile -ExecutionPolicy Bypass -File "{0}" -BootstrapperPath "{1}" -Scenario both -ManagedMutating -HostLabel "package-release-server2022-{2}" -ReportPath "{3}"' -f
            $testScriptPath,
            $bootstrapperPath,
            $versionTag,
            (Join-Path $repoRoot ("build\release\deployment-acceptance-package-release-" + $versionTag + ".server2022.json")))
    }
)

$notes = @(
    "This readiness report is derived from repo state, package metadata, and recorded acceptance artifacts.",
    "The current package is suitable for installer testing because packaged preflight and local mixed-lifecycle acceptance both passed.",
    "Final deployment confidence still depends on the elevated managed Windows 11 pass and a Windows Server 2022 pass."
)
if (-not [string]::IsNullOrWhiteSpace($packageVersionTrackingBaseCommit)) {
    $notes += ("Version tracking currently uses base commit " + $packageVersionTrackingBaseCommit + " for automated bump calculations. Package validation is determined by package commit, packaged acceptance, and repository cleanliness.")
}

$report = [pscustomobject][ordered]@{
    generatedAt = (Get-Date).ToString("o")
    releaseStatus = $releaseStatus
    deploymentGate = $deploymentGate
    repository = [pscustomobject][ordered]@{
        root = $repoRoot
        branch = $branchName
        head = $headFull
        headShort = $headShort
        dirty = $repositoryDirty
        statusEntries = $significantStatusEntries
        aheadOfOrigin = $aheadCount
        behindOrigin = $behindCount
    }
    version = [pscustomobject][ordered]@{
        currentVersion = $versionDocument.current_version
        currentTag = $versionDocument.current_tag
        releasedAt = $versionDocument.released_at
    }
    package = [pscustomobject][ordered]@{
        metadataPath = (Resolve-Path $PackageMetadataPath).Path
        packageName = $packageMetadata.packageName
        version = $packageMetadata.version
        versionTag = $packageMetadata.versionTag
        commit = $packageMetadata.commit
        preset = $packageMetadata.preset
        configuration = $packageMetadata.configuration
        stageDirectory = $packageMetadata.stageDirectory
        zipPath = $packageMetadata.zipPath
        validationPath = $packageMetadata.validationPath
        bootstrapperPath = $packageMetadata.bootstrapperPath
        serviceHostPath = $packageMetadata.serviceHostPath
        shellPath = $packageMetadata.shellPath
        manifestRoot = $packageMetadata.manifestRoot
        webRoot = $packageMetadata.webRoot
        fileCount = $packageMetadata.fileCount
        packageSizeBytes = $packageMetadata.packageSizeBytes
        packagedPreflight = $packageMetadata.packagedPreflight
        versionTrackingBaseCommit = $packageVersionTrackingBaseCommit
    }
    acceptance = [pscustomobject][ordered]@{
        reportPath = (Resolve-Path $AcceptanceReportPath).Path
        generatedAt = $acceptanceReport.generatedAt
        completedAt = $acceptanceReport.completedAt
        scenario = $acceptanceReport.effectiveScenario
        hostLabel = $acceptanceReport.hostLabel
        succeeded = [bool]$acceptanceReport.succeeded
        isAdministrator = [bool]$acceptanceReport.isAdministrator
        managedMutating = [bool]$acceptanceReport.managedMutating
        hostCaption = $acceptanceReport.host.caption
        hostVersion = $acceptanceReport.host.version
        hostBuildNumber = $acceptanceReport.host.buildNumber
        artifactRoot = $acceptanceReport.artifactRoot
        bundlePath = $acceptanceReport.bundlePath
        hostDiagnosticsDirectory = if ($acceptanceReport.PSObject.Properties.Name -contains "hostDiagnostics") { $acceptanceReport.hostDiagnostics.directory } else { "" }
        followUpActions = if ($acceptanceReport.PSObject.Properties.Name -contains "followUpActions") { @($acceptanceReport.followUpActions) } else { @() }
    }
    checks = $checks
    remainingExternalValidation = $remainingExternalValidation
    recommendedCommands = $recommendedCommands
    notes = $notes
}

$reportJson = $report | ConvertTo-Json -Depth 8

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $repoRoot "build\release\mastercontrol-release-readiness.json"
}
if ([string]::IsNullOrWhiteSpace($SummaryPath)) {
    $SummaryPath = [System.IO.Path]::ChangeExtension($OutputPath, ".md")
}

$outputDirectory = Split-Path -Parent $OutputPath
if ($outputDirectory) {
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}
$summaryDirectory = Split-Path -Parent $SummaryPath
if ($summaryDirectory) {
    New-Item -ItemType Directory -Force -Path $summaryDirectory | Out-Null
}

Set-Content -Path $OutputPath -Value $reportJson -Encoding UTF8
Set-Content -Path $SummaryPath -Value (New-ReleaseReadinessSummary -Report $report) -Encoding UTF8

$reportJson
