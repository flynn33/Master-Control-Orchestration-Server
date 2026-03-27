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
    [string]$ReportPath
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
        $process = Start-Process `
            -FilePath $Bootstrapper `
            -ArgumentList $argumentLine `
            -Wait `
            -PassThru `
            -NoNewWindow `
            -RedirectStandardOutput $stdout `
            -RedirectStandardError $stderr

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

    $repair = Invoke-Bootstrapper -Bootstrapper $Bootstrapper -Arguments @(
        "repair",
        $installDirectory,
        "--skip-service",
        "--skip-firewall",
        "--skip-uninstall-registration",
        "--json"
    ) -Environment $environment

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

    $expectations = @(
        (New-Expectation ($preflight.exitCode -eq 0 -and $preflight.json.ready) "Mixed preflight should succeed."),
        (New-Expectation ($install.exitCode -eq 0 -and $install.json.succeeded -and $install.json.validated) "Mixed install should succeed and self-validate."),
        (New-Expectation ($validate.exitCode -eq 0 -and $validate.json.valid) "Mixed validate should succeed."),
        (New-Expectation ($upgrade.exitCode -eq 0 -and $upgrade.json.succeeded -and $upgrade.json.validated) "Mixed upgrade should succeed and self-validate."),
        (New-Expectation ($repair.exitCode -eq 0 -and $repair.json.succeeded -and $repair.json.validated) "Mixed repair should succeed and self-validate."),
        (New-Expectation ($uninstall.exitCode -eq 0 -and $uninstall.json.succeeded) "Mixed uninstall should succeed."),
        (New-Expectation (-not (Test-Path $installDirectory)) "Mixed uninstall should remove the install directory."),
        (New-Expectation (-not (Test-Path $dataDirectory)) "Mixed uninstall should remove the data directory."),
        (New-Expectation ($install.json.shellShortcutPresent -and $install.json.dashboardShortcutPresent) "Mixed install should create user shortcut surfaces."),
        (New-Expectation (-not $uninstall.json.shellShortcutPresent -and -not $uninstall.json.dashboardShortcutPresent) "Mixed uninstall should remove user shortcut surfaces.")
    )

    return [pscustomobject]@{
        scenario = "mixed"
        installDirectory = $installDirectory
        dataDirectory = $dataDirectory
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
            $repair = Invoke-Bootstrapper -Bootstrapper $Bootstrapper -Arguments @(
                "repair",
                $installDirectory,
                "--json"
            ) -Environment $environment
            $uninstall = Invoke-Bootstrapper -Bootstrapper $Bootstrapper -Arguments @(
                "uninstall",
                $installDirectory,
                "--purge-install-dir",
                "--purge-data",
                "--json"
            ) -Environment $environment

            $commands.install = $install
            $commands.validate = $validate
            $commands.upgrade = $upgrade
            $commands.repair = $repair
            $commands.uninstall = $uninstall

            $expectations += @(
                (New-Expectation ($install.exitCode -eq 0 -and $install.json.succeeded -and $install.json.validated) "Managed install should succeed and self-validate."),
                (New-Expectation ($validate.exitCode -eq 0 -and $validate.json.valid) "Managed validate should succeed."),
                (New-Expectation ($upgrade.exitCode -eq 0 -and $upgrade.json.succeeded -and $upgrade.json.validated) "Managed upgrade should succeed and self-validate."),
                (New-Expectation ($repair.exitCode -eq 0 -and $repair.json.succeeded -and $repair.json.validated) "Managed repair should succeed and self-validate."),
                (New-Expectation ($uninstall.exitCode -eq 0 -and $uninstall.json.succeeded) "Managed uninstall should succeed."),
                (New-Expectation (-not (Test-Path $installDirectory)) "Managed uninstall should remove the install directory."),
                (New-Expectation (-not (Test-Path $dataDirectory)) "Managed uninstall should remove the data directory.")
            )
        }
    }

    return [pscustomobject]@{
        scenario = "managed"
        installDirectory = $installDirectory
        dataDirectory = $dataDirectory
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

$reportJson = $report | ConvertTo-Json -Depth 10
if ($ReportPath) {
    $reportDirectory = Split-Path -Parent $ReportPath
    if ($reportDirectory) {
        New-Item -ItemType Directory -Force -Path $reportDirectory | Out-Null
    }
    Set-Content -Path $ReportPath -Value $reportJson -Encoding UTF8
}

$reportJson

if ($report.succeeded -and -not $KeepArtifacts) {
    Remove-Item -Path $Root -Recurse -Force -ErrorAction SilentlyContinue
}

if ($report.succeeded) {
    exit 0
}

exit 1
