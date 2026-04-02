# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

[CmdletBinding()]
param(
    [string]$BootstrapperPath = "",
    [string]$SetupPath = "",
    [string]$Root = (Join-Path $env:TEMP ("mastercontrol-deployment-" + [guid]::NewGuid().ToString())),
    [ValidateSet("auto", "mixed", "managed", "both")]
    [string]$Scenario = "auto",
    [string]$HostLabel = "",
    [switch]$ManagedMutating,
    [switch]$AutoElevateManaged,
    [switch]$KeepArtifacts,
    [string]$ReportPath,
    [string]$SummaryPath
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$scriptStartedAt = Get-Date

if ([string]::IsNullOrWhiteSpace($BootstrapperPath)) {
    $BootstrapperPath = Join-Path $repoRoot "dist\debug\MasterControlBootstrapper.exe"
}

if ([string]::IsNullOrWhiteSpace($SetupPath)) {
    $candidateSetupPath = Join-Path (Split-Path -Parent $BootstrapperPath) "MasterControlOrchestrationServerSetup.exe"
    if (Test-Path $candidateSetupPath) {
        $SetupPath = $candidateSetupPath
    }
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

function Invoke-SetupLauncher {
    param(
        [string]$SetupExecutable,
        [string[]]$Arguments,
        [hashtable]$Environment = @{}
    )

    $savedEnvironment = @{}

    try {
        foreach ($entry in $Environment.GetEnumerator()) {
            $savedEnvironment[$entry.Key] = [Environment]::GetEnvironmentVariable($entry.Key)
            [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value)
        }

        $argumentLine = New-ArgumentLine -Arguments $Arguments
        $startedAt = Get-Date
        $process = Start-Process `
            -FilePath $SetupExecutable `
            -ArgumentList $argumentLine `
            -Wait `
            -PassThru
        $completedAt = Get-Date

        return [pscustomobject]@{
            arguments = $Arguments
            argumentLine = $argumentLine
            startedAt = $startedAt.ToString("o")
            completedAt = $completedAt.ToString("o")
            durationMs = [int][Math]::Round(($completedAt - $startedAt).TotalMilliseconds)
            exitCode = $process.ExitCode
            stdout = ""
            stderr = ""
            json = $null
        }
    } finally {
        foreach ($entry in $Environment.GetEnumerator()) {
            [Environment]::SetEnvironmentVariable($entry.Key, $savedEnvironment[$entry.Key])
        }
    }
}

function Invoke-NativeCapture {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$ArtifactDirectory,
        [string]$Name
    )

    New-Item -ItemType Directory -Force -Path $ArtifactDirectory | Out-Null
    $stdoutPath = Join-Path $ArtifactDirectory ($Name + "-stdout.txt")
    $stderrPath = Join-Path $ArtifactDirectory ($Name + "-stderr.txt")
    $startedAt = Get-Date
    $process = Start-Process `
        -FilePath $FilePath `
        -ArgumentList (New-ArgumentLine -Arguments $Arguments) `
        -Wait `
        -PassThru `
        -NoNewWindow `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath
    $completedAt = Get-Date

    return [pscustomobject][ordered]@{
        name = $Name
        filePath = $FilePath
        arguments = @($Arguments)
        startedAt = $startedAt.ToString("o")
        completedAt = $completedAt.ToString("o")
        durationMs = [int][Math]::Round(($completedAt - $startedAt).TotalMilliseconds)
        exitCode = $process.ExitCode
        stdoutPath = $stdoutPath
        stderrPath = $stderrPath
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
    if ($Report.PSObject.Properties.Name -contains "hostLabel" -and -not [string]::IsNullOrWhiteSpace($Report.hostLabel)) {
        $lines.Add("* Host label: $($Report.hostLabel)")
    }
    $lines.Add("* Administrator: $($Report.isAdministrator)")
    $lines.Add("* Scenario: $($Report.scenario) -> $($Report.effectiveScenario)")
    $lines.Add("* Managed mutating enabled: $($Report.managedMutating)")
    $lines.Add("* Auto-elevate managed: $($Report.autoElevateManaged)")
    $lines.Add("* Overall result: $(if ($Report.succeeded) { 'passed' } else { 'failed' })")
    if ($Report.PSObject.Properties.Name -contains "artifactRoot") {
        $lines.Add("* Artifact root: $($Report.artifactRoot)")
    }
    if ($Report.PSObject.Properties.Name -contains "bundlePath") {
        $lines.Add("* Bundle path: $($Report.bundlePath)")
    }
    if ($Report.PSObject.Properties.Name -contains "hostDiagnostics" -and $Report.hostDiagnostics) {
        $lines.Add("* Host diagnostics: $($Report.hostDiagnostics.directory)")
    }
    $lines.Add("")

    if ($Report.PSObject.Properties.Name -contains "followUpActions" -and $Report.followUpActions.Count -gt 0) {
        $lines.Add("## Follow-Up")
        $lines.Add("")
        foreach ($action in $Report.followUpActions) {
            $lines.Add("* $action")
        }
        $lines.Add("")
    }

    if ($Report.PSObject.Properties.Name -contains "elevatedManagedAttempt" -and $Report.elevatedManagedAttempt) {
        $lines.Add("## Elevated Managed Attempt")
        $lines.Add("")
        $lines.Add("* Attempted: $($Report.elevatedManagedAttempt.attempted)")
        $lines.Add("* Launched: $($Report.elevatedManagedAttempt.launched)")
        $lines.Add("* Canceled: $($Report.elevatedManagedAttempt.canceled)")
        $lines.Add("* Exit code: $($Report.elevatedManagedAttempt.exitCode)")
        $lines.Add("* Report path: $($Report.elevatedManagedAttempt.reportPath)")
        if (-not [string]::IsNullOrWhiteSpace($Report.elevatedManagedAttempt.message)) {
            $lines.Add("* Message: $($Report.elevatedManagedAttempt.message)")
        }
        $lines.Add("")
    }

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

function Capture-HostDiagnostics {
    param(
        [string]$ArtifactRoot,
        [datetime]$StartedAt,
        [datetime]$CompletedAt,
        [bool]$IsAdministrator
    )

    $diagnosticsDirectory = Join-Path $ArtifactRoot "_host"
    New-Item -ItemType Directory -Force -Path $diagnosticsDirectory | Out-Null

    $hostContextPath = Join-Path $diagnosticsDirectory "host-context.json"
    $serviceStatusPath = Join-Path $diagnosticsDirectory "service-status.json"
    $serviceEventsPath = Join-Path $diagnosticsDirectory "service-events.json"
    $uninstallStatusPath = Join-Path $diagnosticsDirectory "uninstall-registration.json"
    $shortcutStatusPath = Join-Path $diagnosticsDirectory "shortcut-surfaces.json"

    $hostContext = [pscustomobject][ordered]@{
        collectedAt = (Get-Date).ToString("o")
        startedAt = $StartedAt.ToString("o")
        completedAt = $CompletedAt.ToString("o")
        machineName = $env:COMPUTERNAME
        userName = [Environment]::UserName
        isAdministrator = $IsAdministrator
        powerShellVersion = $PSVersionTable.PSVersion.ToString()
        operatingSystem = (Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, BuildNumber)
    }
    Set-Content -Path $hostContextPath -Value ($hostContext | ConvertTo-Json -Depth 5) -Encoding UTF8

    $uninstallStatus = [pscustomobject][ordered]@{
        hklm = @(Get-ItemProperty -Path "HKLM:\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MasterControlProgram" -ErrorAction SilentlyContinue |
            Select-Object DisplayName, DisplayVersion, InstallLocation, UninstallString, Publisher)
        hkcu = @(Get-ItemProperty -Path "HKCU:\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MasterControlProgram" -ErrorAction SilentlyContinue |
            Select-Object DisplayName, DisplayVersion, InstallLocation, UninstallString, Publisher)
    }
    Set-Content -Path $uninstallStatusPath -Value ($uninstallStatus | ConvertTo-Json -Depth 5) -Encoding UTF8

    $userProgramsRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::Programs)
    $commonProgramsRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::CommonPrograms)
    $userShortcutDirectory = Join-Path $userProgramsRoot "Master Control Orchestration Server"
    $commonShortcutDirectory = Join-Path $commonProgramsRoot "Master Control Orchestration Server"
    $shortcutStatus = [pscustomobject][ordered]@{
        userProgramsRoot = $userProgramsRoot
        commonProgramsRoot = $commonProgramsRoot
        userShortcutDirectory = $userShortcutDirectory
        commonShortcutDirectory = $commonShortcutDirectory
        userShortcutDirectoryExists = Test-Path $userShortcutDirectory
        commonShortcutDirectoryExists = Test-Path $commonShortcutDirectory
        userEntries = @()
        commonEntries = @()
    }
    if ($shortcutStatus.userShortcutDirectoryExists) {
        $shortcutStatus.userEntries = @(Get-ChildItem -Path $userShortcutDirectory -File -ErrorAction SilentlyContinue |
            Select-Object Name, Length, LastWriteTime)
    }
    if ($shortcutStatus.commonShortcutDirectoryExists) {
        $shortcutStatus.commonEntries = @(Get-ChildItem -Path $commonShortcutDirectory -File -ErrorAction SilentlyContinue |
            Select-Object Name, Length, LastWriteTime)
    }
    Set-Content -Path $shortcutStatusPath -Value ($shortcutStatus | ConvertTo-Json -Depth 5) -Encoding UTF8

    $serviceStatus = [pscustomobject][ordered]@{
        getService = @(Get-Service -Name "MasterControlProgram" -ErrorAction SilentlyContinue |
            Select-Object Name, DisplayName, Status, StartType)
        cimService = @(Get-CimInstance Win32_Service -Filter "Name='MasterControlProgram'" -ErrorAction SilentlyContinue |
            Select-Object Name, DisplayName, State, StartMode, ProcessId, ExitCode, PathName)
    }
    Set-Content -Path $serviceStatusPath -Value ($serviceStatus | ConvertTo-Json -Depth 5) -Encoding UTF8

    $systemEvents = @()
    try {
        $systemEvents = @(Get-WinEvent -FilterHashtable @{
                LogName = "System"
                StartTime = $StartedAt.AddMinutes(-2)
                EndTime = $CompletedAt.AddMinutes(2)
            } -ErrorAction Stop |
            Where-Object {
                $_.ProviderName -eq "Service Control Manager" -and
                $_.Message -match "Master Control Orchestration Server|MasterControlOrchestrationServer|MasterControlProgram"
            } |
            Select-Object TimeCreated, Id, LevelDisplayName, ProviderName, Message)
    } catch {
        $systemEvents = @([pscustomobject]@{
                collectionError = $_.Exception.Message
            })
    }
    Set-Content -Path $serviceEventsPath -Value ($systemEvents | ConvertTo-Json -Depth 6) -Encoding UTF8

    $nativeCaptures = New-Object System.Collections.ArrayList
    [void]$nativeCaptures.Add((Invoke-NativeCapture -FilePath "sc.exe" -Arguments @("queryex", "MasterControlProgram") -ArtifactDirectory $diagnosticsDirectory -Name "sc-queryex"))
    [void]$nativeCaptures.Add((Invoke-NativeCapture -FilePath "sc.exe" -Arguments @("qc", "MasterControlProgram") -ArtifactDirectory $diagnosticsDirectory -Name "sc-qc"))
    [void]$nativeCaptures.Add((Invoke-NativeCapture -FilePath "sc.exe" -Arguments @("qfailure", "MasterControlProgram") -ArtifactDirectory $diagnosticsDirectory -Name "sc-qfailure"))
    [void]$nativeCaptures.Add((Invoke-NativeCapture -FilePath "sc.exe" -Arguments @("qsidtype", "MasterControlProgram") -ArtifactDirectory $diagnosticsDirectory -Name "sc-qsidtype"))
    [void]$nativeCaptures.Add((Invoke-NativeCapture -FilePath "reg.exe" -Arguments @("query", "HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MasterControlProgram", "/s") -ArtifactDirectory $diagnosticsDirectory -Name "reg-uninstall-hklm"))
    [void]$nativeCaptures.Add((Invoke-NativeCapture -FilePath "reg.exe" -Arguments @("query", "HKCU\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MasterControlProgram", "/s") -ArtifactDirectory $diagnosticsDirectory -Name "reg-uninstall-hkcu"))
    [void]$nativeCaptures.Add((Invoke-NativeCapture -FilePath "netsh.exe" -Arguments @("advfirewall", "firewall", "show", "rule", "name=Master Control Orchestration Server - Browser Access") -ArtifactDirectory $diagnosticsDirectory -Name "netsh-browser-rule"))
    [void]$nativeCaptures.Add((Invoke-NativeCapture -FilePath "netsh.exe" -Arguments @("advfirewall", "firewall", "show", "rule", "name=Master Control Orchestration Server - Beacon Discovery") -ArtifactDirectory $diagnosticsDirectory -Name "netsh-beacon-rule"))

    return [pscustomobject][ordered]@{
        directory = $diagnosticsDirectory
        hostContextPath = $hostContextPath
        serviceStatusPath = $serviceStatusPath
        serviceEventsPath = $serviceEventsPath
        uninstallStatusPath = $uninstallStatusPath
        shortcutStatusPath = $shortcutStatusPath
        nativeCaptures = $nativeCaptures
    }
}

function Invoke-ElevatedManagedAcceptance {
    param(
        [string]$Bootstrapper,
        [string]$ReportPath,
        [string]$HostLabel,
        [bool]$KeepArtifacts
    )

    $summaryPath = [System.IO.Path]::ChangeExtension([System.IO.Path]::GetFullPath($ReportPath), ".md")
    $scriptArguments = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $PSCommandPath,
        "-BootstrapperPath", $Bootstrapper,
        "-Scenario", "managed",
        "-ManagedMutating",
        "-HostLabel", $HostLabel,
        "-ReportPath", $ReportPath,
        "-SummaryPath", $summaryPath
    )
    if ($KeepArtifacts) {
        $scriptArguments += "-KeepArtifacts"
    }

    $result = [pscustomobject][ordered]@{
        attempted = $true
        launched = $false
        canceled = $false
        exitCode = $null
        reportPath = $ReportPath
        summaryPath = $summaryPath
        command = "powershell.exe " + (New-ArgumentLine -Arguments $scriptArguments)
        reportLoaded = $false
        childSucceeded = $false
        message = ""
    }

    try {
        $process = Start-Process `
            -FilePath "powershell.exe" `
            -ArgumentList (New-ArgumentLine -Arguments $scriptArguments) `
            -Verb RunAs `
            -Wait `
            -PassThru

        $result.launched = $true
        $result.exitCode = $process.ExitCode
    } catch {
        $result.canceled = $true
        $result.message = $_.Exception.Message
        return $result
    }

    if (Test-Path $ReportPath) {
        try {
            $childReport = Get-Content $ReportPath -Raw | ConvertFrom-Json
            $result.reportLoaded = $true
            $result.childSucceeded = [bool]$childReport.succeeded
        } catch {
            $result.message = "Elevated managed report was written but could not be parsed: $($_.Exception.Message)"
        }
    } elseif ([string]::IsNullOrWhiteSpace($result.message)) {
        $result.message = "Elevated managed report was not produced."
    }

    return $result
}

function Run-MixedAcceptance {
    param(
        [string]$Bootstrapper,
        [string]$ScenarioRoot,
        [string]$SetupExecutable = ""
    )

    $installDirectory = Join-Path $ScenarioRoot "mixed install"
    $dataDirectory = Join-Path $ScenarioRoot "mixed data"
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

    $useSetupLauncher = -not [string]::IsNullOrWhiteSpace($SetupExecutable) -and (Test-Path $SetupExecutable)
    if ($useSetupLauncher) {
        $install = Invoke-SetupLauncher -SetupExecutable $SetupExecutable -Arguments @(
            "--install-directory",
            $installDirectory,
            "--skip-service",
            "--skip-firewall",
            "--skip-uninstall-registration",
            "--quiet",
            "--no-launch-shell"
        ) -Environment $environment
    } else {
        $install = Invoke-Bootstrapper -Bootstrapper $Bootstrapper -Arguments @(
            "install",
            $installDirectory,
            "--skip-service",
            "--skip-firewall",
            "--skip-uninstall-registration",
            "--json"
        ) -Environment $environment
    }
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

    $installValidated = $install.exitCode -eq 0 -and (
        ($null -ne $install.json -and $install.json.PSObject.Properties.Name -contains "validated" -and $install.json.validated) -or
        ($validate.exitCode -eq 0 -and $validate.json.valid)
    )
    $shortcutSurfacesCreated = $validate.exitCode -eq 0 -and $validate.json.shellShortcutPresent -and $validate.json.dashboardShortcutPresent

    $expectations = @(
        (New-Expectation ($preflight.exitCode -eq 0 -and $preflight.json.ready) "Mixed preflight should succeed."),
        (New-Expectation ($installValidated) "Mixed install should succeed and self-validate."),
        (New-Expectation ($validate.exitCode -eq 0 -and $validate.json.valid) "Mixed validate should succeed."),
        (New-Expectation ($upgrade.exitCode -eq 0 -and $upgrade.json.succeeded -and $upgrade.json.validated) "Mixed upgrade should succeed and self-validate."),
        (New-Expectation ($repair.exitCode -eq 0 -and $repair.json.succeeded -and $repair.json.validated) "Mixed repair should succeed and self-validate."),
        (New-Expectation ($uninstall.exitCode -eq 0 -and $uninstall.json.succeeded) "Mixed uninstall should succeed."),
        (New-Expectation ($installDirectoryRemovedAfterUninstall) "Mixed uninstall should remove the install directory."),
        (New-Expectation ($dataDirectoryRemovedAfterUninstall) "Mixed uninstall should remove the data directory."),
        (New-Expectation ($shortcutSurfacesCreated) "Mixed install should create user shortcut surfaces."),
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

    $installDirectory = Join-Path $ScenarioRoot "managed install"
    $dataDirectory = Join-Path $ScenarioRoot "managed data"
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
    $report.scenarios += Run-MixedAcceptance -Bootstrapper $bootstrapper.Path -ScenarioRoot (Join-Path $Root "mixed") -SetupExecutable $SetupPath
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
$bundlePath = if (-not [string]::IsNullOrWhiteSpace($ReportPath)) {
    [System.IO.Path]::ChangeExtension([System.IO.Path]::GetFullPath($ReportPath), ".bundle.zip")
} else {
    Join-Path $artifactRoot "deployment-acceptance.bundle.zip"
}
$scriptCompletedAt = Get-Date

$reportForSerialization = [pscustomobject][ordered]@{
    generatedAt = $report.generatedAt
    completedAt = $scriptCompletedAt.ToString("o")
    bootstrapperPath = $report.bootstrapperPath
    root = $report.root
    artifactRoot = $artifactRoot
    bundlePath = $bundlePath
    scenario = $report.scenario
    effectiveScenario = $report.effectiveScenario
    hostLabel = $HostLabel
    isAdministrator = $report.isAdministrator
    managedMutating = $report.managedMutating
    autoElevateManaged = [bool]$AutoElevateManaged
    host = [pscustomobject][ordered]@{
        caption = $report.host.caption
        version = $report.host.version
        buildNumber = $report.host.buildNumber
        machineName = $report.host.machineName
    }
    scenarios = @($report.scenarios | ForEach-Object { Convert-ScenarioForReport -ScenarioResult $_ -ArtifactRoot $artifactRoot })
    succeeded = $report.succeeded
}
$reportForSerialization | Add-Member -NotePropertyName hostDiagnostics -NotePropertyValue (
    Capture-HostDiagnostics -ArtifactRoot $artifactRoot -StartedAt $scriptStartedAt -CompletedAt $scriptCompletedAt -IsAdministrator:$isAdministrator
)

$managedReportPath = if (-not [string]::IsNullOrWhiteSpace($ReportPath)) {
    [System.IO.Path]::ChangeExtension([System.IO.Path]::GetFullPath($ReportPath), ".managed.json")
} else {
    Join-Path $artifactRoot "managed-elevated.json"
}

if ($AutoElevateManaged -and -not $isAdministrator -and $effectiveScenario -in @("managed", "both")) {
    $managedHostLabel = if ([string]::IsNullOrWhiteSpace($HostLabel)) {
        $env:COMPUTERNAME + "-managed-elevated"
    } else {
        $HostLabel + "-managed-elevated"
    }
    $elevatedManagedAttempt = Invoke-ElevatedManagedAcceptance `
        -Bootstrapper $bootstrapper.Path `
        -ReportPath $managedReportPath `
        -HostLabel $managedHostLabel `
        -KeepArtifacts:$KeepArtifacts
    $reportForSerialization | Add-Member -NotePropertyName elevatedManagedAttempt -NotePropertyValue $elevatedManagedAttempt
    if (-not $elevatedManagedAttempt.childSucceeded) {
        $reportForSerialization.succeeded = $false
    }
}

$followUpActions = New-Object System.Collections.ArrayList
if (-not $report.isAdministrator -and $report.effectiveScenario -in @("managed", "both")) {
    $manualManagedHostLabel = if ([string]::IsNullOrWhiteSpace($HostLabel)) {
        $env:COMPUTERNAME + "-managed-elevated"
    } else {
        $HostLabel + "-managed-elevated"
    }
    $elevatedCommand = @(
        "powershell.exe",
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $PSCommandPath,
        "-BootstrapperPath", $bootstrapper.Path,
        "-Scenario", "managed",
        "-ManagedMutating",
        "-HostLabel", $manualManagedHostLabel,
        "-AutoElevateManaged:$false",
        "-ReportPath", $managedReportPath
    ) -join " "
    if (-not $AutoElevateManaged) {
        [void]$followUpActions.Add("Run the fully managed lifecycle from an elevated PowerShell session: $elevatedCommand")
    }
}

if ($report.host.caption -notmatch "Windows Server 2022") {
    $serverReportPath = if (-not [string]::IsNullOrWhiteSpace($ReportPath)) {
        [System.IO.Path]::ChangeExtension([System.IO.Path]::GetFullPath($ReportPath), ".server2022.json")
    } else {
        "<server-2022-report-path>"
    }
    $serverHostLabel = if ([string]::IsNullOrWhiteSpace($HostLabel)) {
        "server2022"
    } else {
        $HostLabel + "-server2022"
    }
    $serverCommand = @(
        "powershell.exe",
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $PSCommandPath,
        "-BootstrapperPath", $bootstrapper.Path,
        "-Scenario", "both",
        "-HostLabel", $serverHostLabel,
        "-ReportPath", $serverReportPath
    ) -join " "
    [void]$followUpActions.Add("Run the same acceptance harness on a Windows Server 2022 host: $serverCommand")
}

if ($followUpActions.Count -gt 0) {
    $reportForSerialization | Add-Member -NotePropertyName followUpActions -NotePropertyValue @($followUpActions)
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

$bundleSources = New-Object System.Collections.ArrayList
if ($ReportPath -and (Test-Path $ReportPath)) {
    [void]$bundleSources.Add($ReportPath)
}
if ($SummaryPath -and (Test-Path $SummaryPath)) {
    [void]$bundleSources.Add($SummaryPath)
}
if (Test-Path $artifactRoot) {
    [void]$bundleSources.Add($artifactRoot)
}
if ($bundleSources.Count -gt 0) {
    if (Test-Path $bundlePath) {
        Remove-Item -Path $bundlePath -Force
    }
    Compress-Archive -Path @($bundleSources) -DestinationPath $bundlePath -Force
}

$reportJson

if ($report.succeeded -and -not $KeepArtifacts) {
    Remove-Item -Path $Root -Recurse -Force -ErrorAction SilentlyContinue
}

if ($report.succeeded) {
    exit 0
}

exit 1
