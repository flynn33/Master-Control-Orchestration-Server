# MCOS deployed-runtime acceptance probe (NON-DESTRUCTIVE)
#
# Validates an ALREADY-INSTALLED Master Control Orchestration Server instance on
# the local host by observation only. It never installs, uninstalls, starts,
# stops, repairs, upgrades, creates or removes firewall rules or URL ACLs, and
# never binds a certificate. Every check reads existing state.
#
# What it probes (all read-only):
#   1. Installed directory        - uninstall registry / service binary / Program Files
#   2. installation-state.json     - configPath, ports, firewall/service managed flags
#   3. Config path under ProgramData
#   4. Package/version metadata     - installation-state.json + PACKAGE-METADATA.json
#   5. Bootstrapper preflight       - `MasterControlBootstrapper.exe preflight --json` (read-only verb)
#   6. Windows service status       - Get-Service / Get-CimInstance (query only)
#   7. http://localhost:7300/api/health
#   8. http://localhost:7300/api/discovery
#   9. http://localhost:7300/api/gateway/status
#  10. http://localhost:7300/api/clients (presence)
#  11. Firewall rule presence        - netsh advfirewall firewall show rule (read)
#  12. URL ACL presence              - netsh http show urlacl (read)
#
# It emits a JSON report and a Markdown summary and returns exit code 0 when the
# instance is installed, service-running, and locally healthy; otherwise 1.
#
# Passing this probe proves the LOCAL surface is healthy. It does NOT prove
# LAN-discoverability from a second host or live LAN-client interoperability;
# those require the operator-authorized gates documented in
# docs/wiki/Deployment-Acceptance.md. Pass -EmitOperatorGateCommands to print
# (never run) those operator-authorized commands.
#
# Run: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Test-MasterControlOrchestrationServerDeployedRuntime.ps1
#
# Optional parameters:
#   -AdminBaseUrl / -BaseUrl   default http://localhost:7300
#   -ServiceName               default MasterControlProgram
#   -InstallDirectory          override install-dir auto-detection
#   -ConfigPath                override config-path auto-detection
#   -HttpTimeoutSec            default 20
#   -OutputDirectory           directory for JSON and Markdown reports
#   -ReportPath / -SummaryPath explicit report file paths
#   -Strict                    exit nonzero when required probes fail
#   -EmitOperatorGateCommands  print (do not run) the operator-authorized mutating gates

[CmdletBinding()]
param(
    [Alias("BaseUrl")]
    [string]$AdminBaseUrl = "http://localhost:7300",
    [string]$ServiceName = "MasterControlProgram",
    [string]$InstallDirectory = "",
    [string]$ConfigPath = "",
    [int]$HttpTimeoutSec = 20,
    [string]$OutputDirectory = "",
    [string]$ReportPath = "",
    [string]$SummaryPath = "",
    [switch]$Strict,
    [switch]$EmitOperatorGateCommands
)

$ErrorActionPreference = "Continue"
$ProgressPreference = "SilentlyContinue"

# --- source-verified constants (src/MasterControlBootstrapper/main.cpp,
#     src/MasterControlApp/MasterControlDefaults.cpp) ---
$script:ProductName = "Master Control Orchestration Server"
$script:FirewallRuleNames = @(
    "Master Control Orchestration Server - Browser Access",
    "Master Control Orchestration Server - Beacon Discovery",
    "Master Control Orchestration Server - MCP Gateway",
    "Master Control Orchestration Server - DNS-SD mDNS Advertising"
)
$script:UninstallRegistryPath = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\MasterControlProgram"
$script:DefaultGatewayPort = 8080
$script:Probes = New-Object System.Collections.Generic.List[object]

function Add-Probe {
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][string]$Category,
        [Parameter(Mandatory = $true)][ValidateSet("PASS", "FAIL", "WARN", "INFO", "SKIP")][string]$Result,
        [string]$Detail = "",
        [object]$Evidence = $null,
        [bool]$Required = $false
    )
    $script:Probes.Add([pscustomobject][ordered]@{
        id       = $Id
        title    = $Title
        category = $Category
        result   = $Result
        required = $Required
        detail   = $Detail
        evidence = $Evidence
    })
    $color = switch ($Result) {
        "PASS" { "Green" }
        "FAIL" { "Red" }
        "WARN" { "Yellow" }
        "SKIP" { "DarkGray" }
        default { "Gray" }
    }
    Write-Host ("[{0,-4}] {1}" -f $Result, $Title) -ForegroundColor $color
    if ($Detail) { Write-Host "        $Detail" -ForegroundColor DarkGray }
}

function Resolve-InstallDirectory {
    if ($InstallDirectory -and (Test-Path $InstallDirectory)) { return $InstallDirectory }

    # 1. Uninstall registry InstallLocation (written by the MSI / bootstrapper).
    $reg = Get-ItemProperty -Path $script:UninstallRegistryPath -ErrorAction SilentlyContinue
    if ($reg -and $reg.InstallLocation -and (Test-Path $reg.InstallLocation)) {
        return $reg.InstallLocation.TrimEnd('\')
    }

    # 2. Service binary path (QueryServiceConfig equivalent).
    $svc = Get-CimInstance Win32_Service -Filter "Name='$ServiceName'" -ErrorAction SilentlyContinue
    if ($svc -and $svc.PathName) {
        $exe = $svc.PathName.Trim('"').Split('"')[0]
        if ($exe -and (Test-Path $exe)) { return (Split-Path -Parent $exe) }
    }

    # 3. Default Program Files layout.
    $candidate = Join-Path $env:ProgramFiles $script:ProductName
    if (Test-Path $candidate) { return $candidate }

    return ""
}

function Resolve-ConfigPath {
    param([object]$InstallState)
    if ($ConfigPath) { return $ConfigPath }
    if ($InstallState -and $InstallState.configPath) { return $InstallState.configPath }
    $base = Join-Path $env:ProgramData "MasterControlOrchestrationServer\config"
    $current = Join-Path $base "master-control-orchestration-server.json"
    if (Test-Path $current) { return $current }
    $legacy = Join-Path $base "master-control-program.json"
    if (Test-Path $legacy) { return $legacy }
    return $current
}

function Test-FirewallRulePresent {
    param([string]$DisplayName)
    # Read-only: netsh advfirewall firewall show rule prints the rule when it
    # exists and "No rules match the specified criteria." (exit 1) when not.
    try {
        $null = & netsh.exe advfirewall firewall show rule name="$DisplayName" 2>$null | Out-String
        # Use netsh's exit code (0 = rule found, 1 = no match) rather than parsing
        # the English "No rules match" string, which is localized on non-English hosts.
        return ($LASTEXITCODE -eq 0)
    } catch {
        return $false
    }
}

function Get-UrlAclText {
    try { return (& netsh.exe http show urlacl 2>$null | Out-String) } catch { return "" }
}

function Invoke-JsonProbe {
    param([string]$Url)
    return Invoke-RestMethod -Uri $Url -TimeoutSec $HttpTimeoutSec -Method Get
}

function ConvertTo-RedactedJsonText {
    param(
        [Parameter(Mandatory = $true)][object]$InputObject,
        [int]$Depth = 12
    )
    $json = $InputObject | ConvertTo-Json -Depth $Depth
    $secretPropertyPattern = '(?i)("([^"]*(secret|token|password|apikey|api_key|accesskey|access_key)[^"]*)"\s*:\s*)"([^"\\]|\\.)*"'
    $json = [regex]::Replace($json, $secretPropertyPattern, '$1"<redacted>"')
    return $json
}

# ---------------------------------------------------------------------------
Write-Host "MCOS deployed-runtime acceptance probe (non-destructive)" -ForegroundColor Cyan
Write-Host "  AdminBaseUrl: $AdminBaseUrl"
Write-Host "  ServiceName:  $ServiceName"
Write-Host ""

$startedAt = (Get-Date).ToString("o")

# 1. Installed directory ----------------------------------------------------
$resolvedInstallDir = Resolve-InstallDirectory
if ($resolvedInstallDir) {
    Add-Probe -Id "install-dir" -Title "Installed directory located" -Category "install" -Result "PASS" `
        -Detail $resolvedInstallDir -Evidence $resolvedInstallDir -Required $true
} else {
    Add-Probe -Id "install-dir" -Title "Installed directory located" -Category "install" -Result "FAIL" `
        -Detail "No uninstall registration, service binary, or Program Files layout found. MCOS does not appear to be installed on this host." -Required $true
}

# 2. installation-state.json ------------------------------------------------
$installState = $null
if ($resolvedInstallDir) {
    $statePath = Join-Path $resolvedInstallDir "installation-state.json"
    if (Test-Path $statePath) {
        try {
            $installState = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
            Add-Probe -Id "install-state" -Title "installation-state.json readable" -Category "install" -Result "PASS" `
                -Detail ("version={0} browserPort={1} gatewayPort={2} firewallManaged={3} serviceManaged={4}" -f `
                    $installState.version, $installState.browserPort, $installState.gatewayPort, $installState.firewallManaged, $installState.serviceManaged) `
                -Evidence $installState
        } catch {
            Add-Probe -Id "install-state" -Title "installation-state.json readable" -Category "install" -Result "WARN" `
                -Detail "Present but not parseable: $($_.Exception.Message)"
        }
    } else {
        Add-Probe -Id "install-state" -Title "installation-state.json present" -Category "install" -Result "WARN" `
            -Detail "Not found under $resolvedInstallDir (a zip-only/console run may not write it)."
    }
}

# 3. Config path ------------------------------------------------------------
$resolvedConfig = Resolve-ConfigPath -InstallState $installState
if (Test-Path $resolvedConfig) {
    Add-Probe -Id "config-path" -Title "Configuration file present" -Category "config" -Result "PASS" -Detail $resolvedConfig -Evidence $resolvedConfig
} else {
    Add-Probe -Id "config-path" -Title "Configuration file present" -Category "config" -Result "WARN" `
        -Detail "Expected at $resolvedConfig (created on first service start)."
}

# 4. Package/version metadata ----------------------------------------------
$version = $null
if ($installState -and $installState.version) { $version = $installState.version }
if (-not $version -and $resolvedInstallDir) {
    $pkgMeta = Join-Path $resolvedInstallDir "PACKAGE-METADATA.json"
    if (Test-Path $pkgMeta) {
        try { $version = (Get-Content -LiteralPath $pkgMeta -Raw | ConvertFrom-Json).version } catch { }
    }
}
if ($version) {
    Add-Probe -Id "version" -Title "Package/version metadata" -Category "install" -Result "INFO" -Detail "version=$version" -Evidence $version
} else {
    Add-Probe -Id "version" -Title "Package/version metadata" -Category "install" -Result "INFO" -Detail "Version metadata not found."
}

# 5. Bootstrapper preflight (read-only verb) -------------------------------
$preflight = $null
if ($resolvedInstallDir) {
    $bootstrapper = Join-Path $resolvedInstallDir "MasterControlBootstrapper.exe"
    if (Test-Path $bootstrapper) {
        try {
            $raw = & $bootstrapper preflight --json 2>$null | Out-String
            $preflight = $raw | ConvertFrom-Json
            $ready = $false
            if ($null -ne $preflight.ready) { $ready = [bool]$preflight.ready }
            if ($ready) {
                Add-Probe -Id "preflight" -Title "Bootstrapper preflight ready" -Category "preflight" -Result "PASS" -Detail "preflight --json reports ready=true" -Evidence $preflight
            } else {
                Add-Probe -Id "preflight" -Title "Bootstrapper preflight ready" -Category "preflight" -Result "WARN" -Detail "preflight --json reports ready=false or issues present" -Evidence $preflight
            }
        } catch {
            Add-Probe -Id "preflight" -Title "Bootstrapper preflight" -Category "preflight" -Result "WARN" -Detail "Could not run preflight --json: $($_.Exception.Message)"
        }
    } else {
        Add-Probe -Id "preflight" -Title "Bootstrapper preflight" -Category "preflight" -Result "SKIP" -Detail "MasterControlBootstrapper.exe not found under install directory."
    }
} else {
    Add-Probe -Id "preflight" -Title "Bootstrapper preflight" -Category "preflight" -Result "SKIP" -Detail "No install directory."
}

# 6. Windows service status (query only) -----------------------------------
$svc = Get-Service -Name $ServiceName -ErrorAction SilentlyContinue
$cim = Get-CimInstance Win32_Service -Filter "Name='$ServiceName'" -ErrorAction SilentlyContinue
$serviceRunning = $false
if ($svc) {
    $serviceRunning = ($svc.Status -eq "Running")
    $evidence = [pscustomobject][ordered]@{ status = "$($svc.Status)"; startType = "$($svc.StartType)"; state = $cim.State; startMode = $cim.StartMode; pathName = $cim.PathName }
    if ($serviceRunning) {
        Add-Probe -Id "service" -Title "Windows service running" -Category "service" -Result "PASS" -Detail "$ServiceName Status=$($svc.Status) StartType=$($svc.StartType)" -Evidence $evidence -Required $true
    } else {
        Add-Probe -Id "service" -Title "Windows service running" -Category "service" -Result "FAIL" -Detail "$ServiceName Status=$($svc.Status)" -Evidence $evidence -Required $true
    }
} else {
    Add-Probe -Id "service" -Title "Windows service registered" -Category "service" -Result "FAIL" -Detail "Service '$ServiceName' not found." -Required $true
}

# 7-10. HTTP probes ---------------------------------------------------------
$httpTargets = @(
    @{ Id = "http-health";  Title = "GET /api/health";         Path = "/api/health";         Required = $true },
    @{ Id = "http-discovery"; Title = "GET /api/discovery";     Path = "/api/discovery";       Required = $true },
    @{ Id = "http-gateway"; Title = "GET /api/gateway/status";  Path = "/api/gateway/status";  Required = $true },
    @{ Id = "http-clients"; Title = "GET /api/clients";         Path = "/api/clients";         Required = $false }
)
$testedHttpPaths = @()
foreach ($t in $httpTargets) {
    $url = ($AdminBaseUrl.TrimEnd('/')) + $t.Path
    $testedHttpPaths += $url
    try {
        $resp = Invoke-JsonProbe -Url $url
        $summary = switch ($t.Id) {
            "http-health"    { "status=$($resp.status)" }
            "http-discovery" { "instanceName=$($resp.instanceName) serverIp=$($resp.serverIpAddress)" }
            "http-gateway"   { "state=$($resp.state) tlsBound=$($resp.tlsBound)" }
            "http-clients"   { "clientCount=$(@($resp).Count)" }
            default          { "ok" }
        }
        Add-Probe -Id $t.Id -Title $t.Title -Category "http" -Result "PASS" -Detail $summary -Evidence $resp -Required $t.Required
    } catch {
        $res = "FAIL"
        if (-not $t.Required) { $res = "WARN" }
        Add-Probe -Id $t.Id -Title $t.Title -Category "http" -Result $res -Detail "$url unreachable: $($_.Exception.Message)" -Required $t.Required
    }
}

# 11. Firewall rule presence (read-only) -----------------------------------
$firewallManaged = $true
if ($installState -and ($null -ne $installState.firewallManaged)) { $firewallManaged = [bool]$installState.firewallManaged }
$fwPresent = @()
$fwMissing = @()
foreach ($rule in $script:FirewallRuleNames) {
    if (Test-FirewallRulePresent -DisplayName $rule) { $fwPresent += $rule } else { $fwMissing += $rule }
}
$fwEvidence = [pscustomobject][ordered]@{ present = $fwPresent; missing = $fwMissing; firewallManaged = $firewallManaged }
if ($fwPresent.Count -gt 0 -and $fwMissing.Count -eq 0) {
    Add-Probe -Id "firewall" -Title "Firewall rules present" -Category "firewall" -Result "PASS" -Detail "$($fwPresent.Count)/4 'Master Control Orchestration Server -' rules present" -Evidence $fwEvidence
} elseif (-not $firewallManaged) {
    Add-Probe -Id "firewall" -Title "Firewall rules present" -Category "firewall" -Result "INFO" -Detail "firewallManaged=false in installation-state; managed rules not expected. Present=$($fwPresent.Count)/4." -Evidence $fwEvidence
} elseif ($fwPresent.Count -gt 0) {
    Add-Probe -Id "firewall" -Title "Firewall rules present" -Category "firewall" -Result "WARN" -Detail "Only $($fwPresent.Count)/4 managed rules present. Missing: $($fwMissing -join ', ')" -Evidence $fwEvidence
} else {
    Add-Probe -Id "firewall" -Title "Firewall rules present" -Category "firewall" -Result "WARN" -Detail "No 'Master Control Orchestration Server -' firewall rules found (check --skip-firewall / checkbox, or query elevation)." -Evidence $fwEvidence
}

# 12. URL ACL presence (read-only) -----------------------------------------
$gatewayPort = $script:DefaultGatewayPort
if ($installState -and $installState.gatewayPort) { $gatewayPort = [int]$installState.gatewayPort }
$urlAclText = Get-UrlAclText
$urlAclPrefix = "http://+:$gatewayPort/"
if ($urlAclText -and ($urlAclText -match [regex]::Escape($urlAclPrefix))) {
    Add-Probe -Id "urlacl" -Title "Gateway URL ACL reserved" -Category "urlacl" -Result "PASS" -Detail "Reservation present: $urlAclPrefix" -Evidence $urlAclPrefix
} elseif ($urlAclText) {
    Add-Probe -Id "urlacl" -Title "Gateway URL ACL reserved" -Category "urlacl" -Result "INFO" -Detail "No reservation for $urlAclPrefix (LocalSystem service mode does not require it)." -Evidence $urlAclPrefix
} else {
    Add-Probe -Id "urlacl" -Title "Gateway URL ACL reserved" -Category "urlacl" -Result "WARN" -Detail "Could not read URL ACL table (netsh http show urlacl)."
}

# ---------------------------------------------------------------------------
# Overall assessment (conservative; never claims deployment-qualified).
$requiredProbes = @($script:Probes | Where-Object { $_.required })
$requiredFailures = @($requiredProbes | Where-Object { $_.result -eq "FAIL" })
$installed = ($resolvedInstallDir -ne "")
$healthProbe = $script:Probes | Where-Object { $_.id -eq "http-health" }
$localHealthy = ($serviceRunning -and $healthProbe -and $healthProbe.result -eq "PASS")

if (-not $installed) {
    $assessment = "not-installed"
} elseif (-not $serviceRunning) {
    $assessment = "installed-not-running"
} elseif ($localHealthy -and $requiredFailures.Count -eq 0) {
    $assessment = "service-running-local-healthy"
} else {
    $assessment = "degraded"
}

$overall = "FAIL"
if ($requiredFailures.Count -eq 0 -and $installed) { $overall = "PASS" }

$report = [pscustomobject][ordered]@{
    schema         = "mcos.deployed-runtime-acceptance.v1"
    generatedAt    = (Get-Date).ToString("o")
    startedAt      = $startedAt
    host           = $env:COMPUTERNAME
    user           = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
    powershellVersion = $PSVersionTable.PSVersion.ToString()
    adminBaseUrl   = $AdminBaseUrl
    serviceName    = $ServiceName
    testedPaths    = $testedHttpPaths
    installDirectory = $resolvedInstallDir
    configPath     = $resolvedConfig
    version        = $version
    assessment     = $assessment
    overall        = $overall
    strictMode     = [bool]$Strict
    nonDestructive = $true
    redaction      = [pscustomobject][ordered]@{
        secretsRedacted = $true
        tokensRedacted = $true
    }
    note           = "Local surface only. Deployment-qualified status additionally requires operator-authorized second-host LAN discovery and a live LAN-client /api/clients heartbeat (see docs/wiki/Deployment-Acceptance.md)."
    counts         = [pscustomobject][ordered]@{
        pass = @($script:Probes | Where-Object { $_.result -eq "PASS" }).Count
        fail = @($script:Probes | Where-Object { $_.result -eq "FAIL" }).Count
        warn = @($script:Probes | Where-Object { $_.result -eq "WARN" }).Count
        info = @($script:Probes | Where-Object { $_.result -eq "INFO" }).Count
        skip = @($script:Probes | Where-Object { $_.result -eq "SKIP" }).Count
    }
    probes         = $script:Probes
}

# Resolve report paths (default under the local, gitignored artifacts tree).
$repoRoot = Split-Path -Parent $PSScriptRoot
$defaultDir = Join-Path $repoRoot "artifacts\deployability-audit\deployed-runtime"
if ($OutputDirectory) {
    if (-not $ReportPath) { $ReportPath = Join-Path $OutputDirectory "deployed-runtime-report.json" }
    if (-not $SummaryPath) { $SummaryPath = Join-Path $OutputDirectory "deployed-runtime-summary.md" }
} else {
    if (-not $ReportPath) { $ReportPath = Join-Path $defaultDir "deployed-runtime-report.json" }
    if (-not $SummaryPath) { $SummaryPath = Join-Path $defaultDir "deployed-runtime-summary.md" }
}
$reportDir = Split-Path -Parent $ReportPath
if ($reportDir -and -not (Test-Path $reportDir)) { New-Item -ItemType Directory -Force -Path $reportDir | Out-Null }
$summaryDir = Split-Path -Parent $SummaryPath
if ($summaryDir -and -not (Test-Path $summaryDir)) { New-Item -ItemType Directory -Force -Path $summaryDir | Out-Null }

Set-Content -Path $ReportPath -Value (ConvertTo-RedactedJsonText -InputObject $report -Depth 12) -Encoding UTF8

# Markdown summary.
$md = New-Object System.Text.StringBuilder
[void]$md.AppendLine("# MCOS Deployed-Runtime Acceptance (non-destructive)")
[void]$md.AppendLine("")
[void]$md.AppendLine("- Host: ``$($env:COMPUTERNAME)``")
[void]$md.AppendLine("- User: ``$($report.user)``")
[void]$md.AppendLine("- PowerShell: ``$($report.powershellVersion)``")
[void]$md.AppendLine("- Generated: $($report.generatedAt)")
[void]$md.AppendLine("- Base URL: ``$AdminBaseUrl``")
[void]$md.AppendLine("- Service: ``$ServiceName``")
[void]$md.AppendLine("- Install directory: ``$resolvedInstallDir``")
[void]$md.AppendLine("- Config path: ``$resolvedConfig``")
[void]$md.AppendLine("- Assessment: **$assessment**  |  Overall: **$overall**")
[void]$md.AppendLine("- Counts: PASS=$($report.counts.pass) FAIL=$($report.counts.fail) WARN=$($report.counts.warn) INFO=$($report.counts.info) SKIP=$($report.counts.skip)")
[void]$md.AppendLine("")
[void]$md.AppendLine("> Local surface only. Deployment-qualified status additionally requires operator-authorized")
[void]$md.AppendLine("> second-host LAN discovery and a live LAN-client heartbeat (see Deployment-Acceptance).")
[void]$md.AppendLine("")
[void]$md.AppendLine("| Result | Check | Detail |")
[void]$md.AppendLine("|---|---|---|")
foreach ($p in $script:Probes) {
    $detail = ($p.detail -replace '\|', '\|')
    [void]$md.AppendLine("| $($p.result) | $($p.title) | $detail |")
}
Set-Content -Path $SummaryPath -Value ($md.ToString()) -Encoding UTF8

Write-Host ""
Write-Host "Assessment: $assessment (overall $overall)" -ForegroundColor Cyan
Write-Host "JSON report:    $ReportPath"
Write-Host "Markdown report: $SummaryPath"

if ($EmitOperatorGateCommands) {
    Write-Host ""
    Write-Host "=== Operator-authorized gates (NOT RUN - require explicit authorization on the target host) ===" -ForegroundColor Yellow
    @(
        "# Gate D managed install, service control, firewall, URL ACL, and TLS actions mutate the host.",
        "# Gate E requires a second LAN host and at least one real LAN client.",
        "# See docs/wiki/Deployment-Acceptance.md for the operator-authorized commands.",
        "# This script intentionally prints no executable mutating commands."
    ) | ForEach-Object { Write-Host "  $_" -ForegroundColor DarkGray }
}

if ($Strict -and $overall -ne "PASS") { exit 1 }
exit 0
