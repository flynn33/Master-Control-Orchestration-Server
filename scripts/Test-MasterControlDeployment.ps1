# Master Control Program
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

[CmdletBinding()]
param(
    [string]$BootstrapperPath = "",
    [string]$Root = (Join-Path $env:TEMP ("mastercontrol-deployment-" + [guid]::NewGuid().ToString())),
    [ValidateSet("auto", "mixed", "managed", "both")]
    [string]$Scenario = "auto",
    [switch]$ManagedMutating,
    [switch]$KeepArtifacts,
    [string]$ReportPath,
    [string]$SummaryPath
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($BootstrapperPath)) {
    $BootstrapperPath = Join-Path $repoRoot "dist\debug\MasterControlBootstrapper.exe"
}

function Test-IsAdministrator {
    return ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).
        IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function New-ArgumentLine {
    param([string[]]$Arguments)

    return [string]::Join(
        " ",
        ($Arguments | ForEach-Object {
            if ($_ -match "\s") { '"' + $_ + '"' } else { $_ }
        }))
}

function Invoke-Bootstrapper {
    param(
        [string]$Bootstrapper,
        [string[]]$Arguments,
        [hashtable]$Environment = @{}
    )

    $stdout = [System.IO.Path]::GetTempFileName()
    $stderr = [System.IO.Path]::GetTempFileName()
    $savedEnvironment = @{}

    try {
        foreach ($entry in $Environment.GetEnumerator()) {
            $savedEnvironment[$entry.Key] = [Environment]::GetEnvironmentVariable($entry.Key)
            [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value)
        }

        $argumentLine = New-ArgumentLine -Arguments $Arguments
        $startedAt = Get-Date
        $process = Start-Process `
            -FilePath $Bootstrapper `
            -ArgumentList $argumentLine `
            -Wait `
            -PassThru `
            -NoNewWindow `
            -RedirectStandardOutput $stdout `
            -RedirectStandardError $stderr
        $completedAt = Get-Date

        $stdOut = Get-Content $stdout -Raw
        $stdErr = Get-Content $stderr -Raw
        $parsedJson = $null
        if (-not [string]::IsNullOrWhiteSpace($stdOut)) {
            $trimmed = $stdOut.Trim()
            if ($trimmed.StartsWith("{") -or $trimmed.StartsWith("[")) {
                try {
                    $parsedJson = $trimmed | ConvertFrom-Json
                } catch {
                    $parsedJson = $null
                }
            }
        }

        return [pscustomobject]@{
            arguments = $Arguments
            argumentLine = $argumentLine
            startedAt = $startedAt.ToString("o")
            completedAt = $completedAt.ToString("o")
            durationMs = [int][Math]::Round(($completedAt - $startedAt).TotalMilliseconds)
            exitCode = $process.ExitCode
            stdout = $stdOut
            stderr = $stdErr
            json = $parsedJson
        }
    } finally {
        foreach ($entry in $Environment.GetEnumerator()) {
            [Environment]::SetEnvironmentVariable($entry.Key, $savedEnvironment[$entry.Key])
        }
        if (-not [string]::IsNullOrWhiteSpace($stdout) -and (Test-Path $stdout)) { Remove-Item $stdout -Force }
        if (-not [string]::IsNullOrWhiteSpace($stderr) -and (Test-Path $stderr)) { Remove-Item $stderr -Force }
    }
}

function Capture-DetectSnapshot {
    param(
        [string]$Bootstrapper,
        [hashtable]$Environment = @{}
    )

    return Invoke-Bootstrapper -Bootstrapper $Bootstrapper -Arguments @(
        "detect",
        "--json"
    ) -Environment $Environment
}

function New-Expectation {
    param(
        [bool]$Passed,
        [string]$Message
    )

    return [pscustomobject]@{
        passed = $Passed
        message = $Message
    }
}

function Test-ScenarioSucceeded {
    param([object[]]$Expectations)

    if ($null -eq $Expectations) {
        return $true
    }

    foreach ($expectation in $Expectations) {
        if (-not $expectation.passed) {
            return $false
        }
    }

    return $true
}

function New-DeploymentSummary {
    param([object]$Report)

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# Master Control Deployment Acceptance Summary")
    $lines.Add("")
    $lines.Add("* Generated: $($Report.generatedAt)")
    $lines.Add("* Host: $($Report.host.caption) ($($Report.host.version) build $($Report.host.buildNumber))")
    $lines.Add("* Machine: $($Report.host.machineName)")
    $lines.Add("* Administrator: $($Report.isAdministrator)")
    $lines.Add("* Scenario: $($Report.scenario) -> $($Report.effectiveScenario)")
    $lines.Add("* Managed mutating enabled: $($Report.managedMutating)")
    $lines.Add("* Overall result: $(if ($Report.succeeded) { 'passed' } else { 'failed' })")
    $lines.Add("")

    foreach ($scenarioResult in $Report.scenarios) {
        $lines.Add("## $($scenarioResult.scenario)")
        $lines.Add("")
        $lines.Add("* Result: $(if ($scenarioResult.succeeded) { 'passed' } else { 'failed' })")
        $lines.Add("* Install directory: $($scenarioResult.installDirectory)")
        $lines.Add("* Data directory: $($scenarioResult.dataDirectory)")
        if ($scenarioResult.PSObject.Properties.Name -contains "mutating") {
            $lines.Add("* Mutating flow: $($scenarioResult.mutating)")
        }
        if ($scenarioResult.PSObject.Properties.Name -contains "snapshots" -and $scenarioResult.snapshots) {
            $snapshotNames = @($scenarioResult.snapshots | ForEach-Object { $_.name })
            if ($snapshotNames.Count -gt 0) {
                $lines.Add("* Detect snapshots: $([string]::Join(', ', $snapshotNames))")
            }
        }
        $lines.Add("")
        $lines.Add("### Expectations")
        $lines.Add("")
        foreach ($expectation in $scenarioResult.expectations) {
            $marker = if ($expectation.passed) { "[pass]" } else { "[fail]" }
            $lines.Add("* $marker $($expectation.message)")
        }
        $lines.Add("")
    }

    return [string]::Join([Environment]::NewLine, $lines)
}

function Convert-CommandForReport {
    param(
        [object]$CommandResult,
        [string]$ArtifactDirectory,
        [string]$Name
    )

    if ($null -eq $CommandResult) {
        return $null
    }

    New-Item -ItemType Directory -Force -Path $ArtifactDirectory | Out-Null
    $stdoutPath = Join-Path $ArtifactDirectory ($Name + "-stdout.txt")
    $stderrPath = Join-Path $ArtifactDirectory ($Name + "-stderr.txt")
    Set-Content -Path $stdoutPath -Value $CommandResult.stdout -Encoding UTF8
    Set-Content -Path $stderrPath -Value $CommandResult.stderr -Encoding UTF8

    return [pscustomobject][ordered]@{
        name = $Name
        arguments = @($CommandResult.arguments)
        argumentLine = $CommandResult.argumentLine
        startedAt = $CommandResult.startedAt
        completedAt = $CommandResult.completedAt
        durationMs = $CommandResult.durationMs
        exitCode = $CommandResult.exitCode
        stdoutPath = $stdoutPath
        stderrPath = $stderrPath
    }
}

function Convert-ScenarioForReport {
    param(
        [object]$ScenarioResult,
        [string]$ArtifactRoot
    )

    $scenarioArtifactDirectory = Join-Path $ArtifactRoot $ScenarioResult.scenario
    $commands = New-Object System.Collections.ArrayList
    foreach ($entry in $ScenarioResult.commands.GetEnumerator()) {
        [void]$commands.Add((Convert-CommandForReport -CommandResult $entry.Value -ArtifactDirectory $scenarioArtifactDirectory -Name $entry.Key))
    }

    $snapshots = New-Object System.Collections.ArrayList
    if ($ScenarioResult.PSObject.Properties.Name -contains "snapshots" -and $ScenarioResult.snapshots) {
        foreach ($entry in $ScenarioResult.snapshots.GetEnumerator()) {
            [void]$snapshots.Add((Convert-CommandForReport -CommandResult $entry.Value -ArtifactDirectory $scenarioArtifactDirectory -Name ("snapshot-" + $entry.Key)))
        }
    }

    $payload = [ordered]@{
        scenario = $ScenarioResult.scenario
        installDirectory = $ScenarioResult.installDirectory
        dataDirectory = $ScenarioResult.dataDirectory
        commands = $commands
        snapshots = $snapshots
        expectations = @($ScenarioResult.expectations)
        succeeded = $ScenarioResult.succeeded
    }

    if ($ScenarioResult.PSObject.Properties.Name -contains "mutating") {
        $payload["mutating"] = $ScenarioResult.mutating
    }

    return [pscustomobject]$payload
}

function Run-MixedAcceptance {
    param(
        [string]$Bootstrapper,
        [string]$ScenarioRoot
    )

    $installDirectory = Join-Path $ScenarioRoot "mixed-install"
    $dataDirectory = Join-Path $ScenarioRoot "mixed-data"
    New-Item -ItemType Directory -Force -Path $ScenarioRoot, $dataDirectory | Out-Null

    $environment = @{
        MASTERCONTROL_DATA_DIR = $dataDirectory
    }
    $snapshots = [ordered]@{
        before = Capture-DetectSnapshot -Bootstrapper $Bootstrapper -Environment $environment
    }

    $preflight = Invoke-Bootstrapper -Bootstrapper $Bootstrapper -Arguments @(
        "preflight",
        $installDirectory,
        "--skip-service",
        "--skip-firewall",
        "--skip-uninstall-registration",
        "--json"
    ) -Environment $environment

    $install = Invoke-Bootstrapper -Bootstrapper $Bootstrapper -Arguments @(
        "install",
        $installDirectory,
        "--skip-service",
        "--skip-firewall",
        "--skip-uninstall-registration",
        "--json"
    ) -Environment $environment
    $snapshots.afterInstall = Capture-DetectSnapshot -Bootstrapper $Bootstrapper -Environment $environment

    $validate = Invoke-Bootstrapper -Bootstrapper $Bootstrapper -Arguments @(
        "validate",
        $installDirectory,
        "--json"
    ) -Environment $environment

    $upgrade = Invoke-Bootstrapper -Bootstrapper $Bootstrapper -Arguments @(
        "upgrade",
        $installDirectory,
        "--skip-service",
        "--skip-firewall",
        "--skip-uninstall-registration",
        "--json"
    ) -Environment $environment
    $snapshots.afterUpgrade = Capture-DetectSnapshot -Bootstrapper $Bootstrapper -Environment $environment

    $repair = Invoke-Bootstrapper -Bootstrapper $Bootstrapper -Arguments @(
        "repair",
        $installDirectory,
        "--skip-service",
        "--skip-firewall",
        "--skip-uninstall-registration",
        "--json"
    ) -Environment $environment
    $snapshots.afterRepair = Capture-DetectSnapshot -Bootstrapper $Bootstrapper -Environment $environment

    $uninstall = Invoke-Bootstrapper -Bootstrapper $Bootstrapper -Arguments @(
        "uninstall",
        $installDirectory,
        "--purge-install-dir",
        "--purge-data",
        "--skip-service",
        "--skip-firewall",
        "--skip-uninstall-registration",
        "--json"
    ) -Environment $environment
    $installDirectoryRemovedAfterUninstall = -not (Test-Path $installDirectory)
    $dataDirectoryRemovedAfterUninstall = -not (Test-Path $dataDirectory)
    $snapshots.afterUninstall = Capture-DetectSnapshot -Bootstrapper $Bootstrapper -Environment $environment

    $expectations = @(
        (New-Expectation ($preflight.exitCode -eq 0 -and $preflight.json.ready) "Mixed preflight should succeed."),
        (New-Expectation ($install.exitCode -eq 0 -and $install.json.succeeded -and $install.json.validated) "Mixed install should succeed and self-validate."),
        (New-Expectation ($validate.exitCode -eq 0 -and $validate.json.valid) "Mixed validate should succeed."),
        (New-Expectation ($upgrade.exitCode -eq 0 -and $upgrade.json.succeeded -and $upgrade.json.validated) "Mixed upgrade should succeed and self-validate."),
        (New-Expectation ($repair.exitCode -eq 0 -and $repair.json.succeeded -and $repair.json.validated) "Mixed repair should succeed and self-validate."),
        (New-Expectation ($uninstall.exitCode -eq 0 -and $uninstall.json.succeeded) "Mixed uninstall should succeed."),
        (New-Expectation ($installDirectoryRemovedAfterUninstall) "Mixed uninstall should remove the install directory."),
        (New-Expectation ($dataDirectoryRemovedAfterUninstall) "Mixed uninstall should remove the data directory."),
        (New-Expectation ($install.json.shellShortcutPresent -and $install.json.dashboardShortcutPresent) "Mixed install should create user shortcut surfaces."),
        (New-Expectation (-not $uninstall.json.shellShortcutPresent -and -not $uninstall.json.dashboardShortcutPresent) "Mixed uninstall should remove user shortcut surfaces."),
        (New-Expectation ($snapshots.afterInstall.exitCode -eq 0 -and -not $snapshots.afterInstall.json.serviceRegistered -and -not $snapshots.afterInstall.json.uninstallRegistered) "Mixed install should leave managed service and uninstall registration absent."),
        (New-Expectation ($snapshots.afterUninstall.exitCode -eq 0 -and -not $snapshots.afterUninstall.json.serviceRegistered -and -not $snapshots.afterUninstall.json.uninstallRegistered) "Mixed uninstall should leave managed integrations absent.")
    )

    return [pscustomobject]@{
        scenario = "mixed"
        installDirectory = $installDirectory
        dataDirectory = $dataDirectory
        snapshots = $snapshots
        commands = [ordered]@{
            preflight = $preflight
            install = $install
            validate = $validate
            upgrade = $upgrade
            repair = $repair
            uninstall = $uninstall
        }
        expectations = $expectations
        succeeded = (Test-ScenarioSucceeded -Expectations $expectations)
    }
}

function Run-ManagedAcceptance {
    param(
        [string]$Bootstrapper,
        [string]$ScenarioRoot,
        [bool]$IsAdministrator,
        [bool]$RunMutating
    )

    $installDirectory = Join-Path $ScenarioRoot "managed-install"
    $dataDirectory = Join-Path $ScenarioRoot "managed-data"
    New-Item -ItemType Directory -Force -Path $ScenarioRoot, $dataDirectory | Out-Null

    $environment = @{
        MASTERCONTROL_DATA_DIR = $dataDirectory
    }
    $snapshots = [ordered]@{
        before = Capture-DetectSnapshot -Bootstrapper $Bootstrapper -Environment $environment
    }

    $preflight = Invoke-Bootstrapper -Bootstrapper $Bootstrapper -Arguments @(
        "preflight",
        $installDirectory,
        "--json"
    ) -Environment $environment

    $commands = [ordered]@{
        preflight = $preflight
    }

    $expectations = @()

    if (-not $IsAdministrator) {
        $issueText = @($preflight.json.issues) -join "`n"
        $expectations += New-Expectation (
            $preflight.exitCode -ne 0 -and $issueText -match "Administrator elevation is required"
        ) "Managed preflight should block on a non-admin host."
    } else {
        $expectations += New-Expectation ($preflight.exitCode -eq 0 -and $preflight.json.ready) "Managed preflight should succeed on an elevated host."

        if ($RunMutating) {
            $install = Invoke-Bootstrapper -Bootstrapper $Bootstrapper -Arguments @(
                "install",
                $installDirectory,
                "--json"
            ) -Environment $environment
            $snapshots.afterInstall = Capture-DetectSnapshot -Bootstrapper $Bootstrapper -Environment $environment
            $validate = Invoke-Bootstrapper -Bootstrapper $Bootstrapper -Arguments @(
                "validate",
                $installDirectory,
                "--json"
            ) -Environment $environment
            $upgrade = Invoke-Bootstrapper -Bootstrapper $Bootstrapper -Arguments @(
                "upgrade",
                $installDirectory,
                "--json"
            ) -Environment $environment
            $snapshots.afterUpgrade = Capture-DetectSnapshot -Bootstrapper $Bootstrapper -Environment $environment
            $repair = Invoke-Bootstrapper -Bootstrapper $Bootstrapper -Arguments @(
                "repair",
                $installDirectory,
                "--json"
            ) -Environment $environment
            $snapshots.afterRepair = Capture-DetectSnapshot -Bootstrapper $Bootstrapper -Environment $environment
            $uninstall = Invoke-Bootstrapper -Bootstrapper $Bootstrapper -Arguments @(
                "uninstall",
                $installDirectory,
                "--purge-install-dir",
                "--purge-data",
                "--json"
            ) -Environment $environment
            $installDirectoryRemovedAfterUninstall = -not (Test-Path $installDirectory)
            $dataDirectoryRemovedAfterUninstall = -not (Test-Path $dataDirectory)
            $snapshots.afterUninstall = Capture-DetectSnapshot -Bootstrapper $Bootstrapper -Environment $environment

            $commands.install = $install
            $commands.validate = $validate
            $commands.upgrade = $upgrade
            $commands.repair = $repair
            $commands.uninstall = $uninstall

            $expectations += @(
                (New-Expectation ($install.exitCode -eq 0 -and $install.json.succeeded -and $install.json.validated) "Managed install should succeed and self-validate."),
                (New-Expectation ($install.json.serviceManaged -and $install.json.serviceRegistered -and $install.json.serviceRunning -and $install.json.serviceAutoStart -and $install.json.serviceDelayedAutoStart -and $install.json.serviceRecoveryConfigured -and $install.json.serviceFailureActionsOnNonCrash -and $install.json.serviceSidUnrestricted) "Managed install should register a running auto-start Windows service with recovery and SID policies."),
                (New-Expectation ($install.json.uninstallRegistered) "Managed install should create Programs and Features registration."),
                (New-Expectation ($install.json.shellShortcutPresent -and $install.json.dashboardShortcutPresent) "Managed install should create Start Menu shortcut surfaces."),
                (New-Expectation ($snapshots.afterInstall.exitCode -eq 0 -and $snapshots.afterInstall.json.serviceRegistered -and $snapshots.afterInstall.json.serviceRunning -and $snapshots.afterInstall.json.serviceDelayedAutoStart -and $snapshots.afterInstall.json.serviceRecoveryConfigured -and $snapshots.afterInstall.json.uninstallRegistered) "Managed detect snapshot after install should show service and uninstall registration in place."),
                (New-Expectation ($validate.exitCode -eq 0 -and $validate.json.valid) "Managed validate should succeed."),
                (New-Expectation ($upgrade.exitCode -eq 0 -and $upgrade.json.succeeded -and $upgrade.json.validated) "Managed upgrade should succeed and self-validate."),
                (New-Expectation ($repair.exitCode -eq 0 -and $repair.json.succeeded -and $repair.json.validated) "Managed repair should succeed and self-validate."),
                (New-Expectation ($uninstall.exitCode -eq 0 -and $uninstall.json.succeeded) "Managed uninstall should succeed."),
                (New-Expectation (-not $uninstall.json.serviceRegistered -and -not $uninstall.json.uninstallRegistered) "Managed uninstall should remove service and Programs and Features registration."),
                (New-Expectation (-not $uninstall.json.shellShortcutPresent -and -not $uninstall.json.dashboardShortcutPresent) "Managed uninstall should remove Start Menu shortcut surfaces."),
                (New-Expectation ($snapshots.afterUninstall.exitCode -eq 0 -and -not $snapshots.afterUninstall.json.serviceRegistered -and -not $snapshots.afterUninstall.json.uninstallRegistered) "Managed detect snapshot after uninstall should show service and uninstall registration removed."),
                (New-Expectation ($installDirectoryRemovedAfterUninstall) "Managed uninstall should remove the install directory."),
                (New-Expectation ($dataDirectoryRemovedAfterUninstall) "Managed uninstall should remove the data directory.")
            )
        }
    }

    return [pscustomobject]@{
        scenario = "managed"
        installDirectory = $installDirectory
        dataDirectory = $dataDirectory
        snapshots = $snapshots
        commands = $commands
        expectations = $expectations
        mutating = $RunMutating
        succeeded = (Test-ScenarioSucceeded -Expectations $expectations)
    }
}

$bootstrapper = Resolve-Path $BootstrapperPath
$isAdministrator = Test-IsAdministrator
$effectiveScenario = switch ($Scenario) {
    "auto" { "both" }
    default { $Scenario }
}

$os = Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, BuildNumber
$report = [ordered]@{
    generatedAt = (Get-Date).ToString("o")
    bootstrapperPath = $bootstrapper.Path
    root = $Root
    scenario = $Scenario
    effectiveScenario = $effectiveScenario
    isAdministrator = $isAdministrator
    managedMutating = [bool]$ManagedMutating
    host = [ordered]@{
        caption = $os.Caption
        version = $os.Version
        buildNumber = $os.BuildNumber
        machineName = $env:COMPUTERNAME
    }
    scenarios = @()
}

if ($effectiveScenario -in @("mixed", "both")) {
    $report.scenarios += Run-MixedAcceptance -Bootstrapper $bootstrapper.Path -ScenarioRoot (Join-Path $Root "mixed")
}

if ($effectiveScenario -in @("managed", "both")) {
    $report.scenarios += Run-ManagedAcceptance `
        -Bootstrapper $bootstrapper.Path `
        -ScenarioRoot (Join-Path $Root "managed") `
        -IsAdministrator:$isAdministrator `
        -RunMutating:$ManagedMutating
}

$report.succeeded = $true
foreach ($scenarioResult in $report.scenarios) {
    if (-not $scenarioResult.succeeded) {
        $report.succeeded = $false
        break
    }
}

$artifactRoot = if (-not [string]::IsNullOrWhiteSpace($ReportPath)) {
    [System.IO.Path]::ChangeExtension([System.IO.Path]::GetFullPath($ReportPath), ".artifacts")
} else {
    Join-Path $report.root "artifacts"
}

$reportForSerialization = [pscustomobject][ordered]@{
    generatedAt = $report.generatedAt
    bootstrapperPath = $report.bootstrapperPath
    root = $report.root
    artifactRoot = $artifactRoot
    scenario = $report.scenario
    effectiveScenario = $report.effectiveScenario
    isAdministrator = $report.isAdministrator
    managedMutating = $report.managedMutating
    host = [pscustomobject][ordered]@{
        caption = $report.host.caption
        version = $report.host.version
        buildNumber = $report.host.buildNumber
        machineName = $report.host.machineName
    }
    scenarios = @($report.scenarios | ForEach-Object { Convert-ScenarioForReport -ScenarioResult $_ -ArtifactRoot $artifactRoot })
    succeeded = $report.succeeded
}

$reportJson = $reportForSerialization | ConvertTo-Json -Depth 10
if ($ReportPath) {
    $reportDirectory = Split-Path -Parent $ReportPath
    if ($reportDirectory) {
        New-Item -ItemType Directory -Force -Path $reportDirectory | Out-Null
    }
    Set-Content -Path $ReportPath -Value $reportJson -Encoding UTF8
}

if (-not [string]::IsNullOrWhiteSpace($ReportPath) -and [string]::IsNullOrWhiteSpace($SummaryPath)) {
    $resolvedReportPath = [System.IO.Path]::GetFullPath($ReportPath)
    $SummaryPath = [System.IO.Path]::ChangeExtension($resolvedReportPath, ".md")
}

if (-not [string]::IsNullOrWhiteSpace($SummaryPath)) {
    $summaryDirectory = Split-Path -Parent $SummaryPath
    if ($summaryDirectory) {
        New-Item -ItemType Directory -Force -Path $summaryDirectory | Out-Null
    }
    Set-Content -Path $SummaryPath -Value (New-DeploymentSummary -Report $reportForSerialization) -Encoding UTF8
}

$reportJson

if ($report.succeeded -and -not $KeepArtifacts) {
    Remove-Item -Path $Root -Recurse -Force -ErrorAction SilentlyContinue
}

if ($report.succeeded) {
    exit 0
}

exit 1
