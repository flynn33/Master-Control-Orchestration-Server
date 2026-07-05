<#
.SYNOPSIS
  Captures non-destructive Gate E LAN discovery evidence from a peer host.

.DESCRIPTION
  Runs from a second host on the trusted LAN. The script probes the MCOS
  operator discovery endpoints over HTTP, optionally captures DNS-SD/mDNS
  browse output when dns-sd is installed, optionally listens for the UDP beacon,
  and writes JSON plus Markdown evidence. It never installs, uninstalls, starts,
  stops, restarts, or reconfigures services, firewall rules, URL ACLs, TLS, or
  MCOS configuration.
#>
[CmdletBinding(DefaultParameterSetName = "Host")]
param(
    [Parameter(Mandatory = $true, ParameterSetName = "Host")]
    [string]$ServerHost,

    [Parameter(Mandatory = $true, ParameterSetName = "Address")]
    [string]$ServerAddress,

    [int]$AdminPort = 7300,
    [int]$GatewayPort = 8080,
    [int]$BeaconPort = 0,
    [int]$HttpTimeoutSec = 20,
    [int]$DiscoveryTimeoutSec = 8,
    [string]$OutputDirectory = "",
    [string]$ReportPath = "",
    [string]$SummaryPath = "",
    [switch]$Strict,
    [switch]$RequireClient
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Continue"
$ProgressPreference = "SilentlyContinue"

$target = if ($PSCmdlet.ParameterSetName -eq "Address") { $ServerAddress } else { $ServerHost }
$startedAt = (Get-Date).ToString("o")
$probes = New-Object System.Collections.Generic.List[object]

function Add-Probe {
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [Parameter(Mandatory = $true)][string]$Title,
        [Parameter(Mandatory = $true)][ValidateSet("PASS", "FAIL", "WARN", "INFO", "SKIP")][string]$Result,
        [bool]$Required = $false,
        [string]$Detail = "",
        [object]$Evidence = $null
    )

    $script:probes.Add([pscustomobject][ordered]@{
        id = $Id
        title = $Title
        result = $Result
        required = $Required
        detail = $Detail
        evidence = $Evidence
    }) | Out-Null

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

function Invoke-JsonHttpProbe {
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Title,
        [bool]$Required = $true
    )

    $url = "http://$target`:$AdminPort$Path"
    try {
        $response = Invoke-RestMethod -Uri $url -TimeoutSec $HttpTimeoutSec -Method Get
        Add-Probe -Id $Id -Title $Title -Result "PASS" -Required $Required -Detail $url -Evidence $response
        return [pscustomobject][ordered]@{ url = $url; passed = $true; response = $response; error = "" }
    } catch {
        $result = if ($Required) { "FAIL" } else { "WARN" }
        Add-Probe -Id $Id -Title $Title -Result $result -Required $Required -Detail "$url failed: $($_.Exception.Message)"
        return [pscustomobject][ordered]@{ url = $url; passed = $false; response = $null; error = $_.Exception.Message }
    }
}

function Get-ObjectPropertyValue {
    param(
        [object]$InputObject,
        [Parameter(Mandatory = $true)][string]$Name
    )

    if ($null -eq $InputObject) { return $null }
    $property = $InputObject.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function Invoke-DnsSdBrowse {
    $dnsSd = Get-Command dns-sd -ErrorAction SilentlyContinue
    if (-not $dnsSd) {
        Add-Probe -Id "dns-sd" -Title "DNS-SD/mDNS browse" -Result "SKIP" -Detail "dns-sd command not found on this peer."
        return [pscustomobject][ordered]@{
            attempted = $false
            available = $false
            passed = $false
            command = $null
            output = ""
            reasonIfUnavailable = "dns-sd command not found on this peer."
        }
    }

    $stdout = [System.IO.Path]::GetTempFileName()
    $stderr = [System.IO.Path]::GetTempFileName()
    $process = $null
    try {
        $process = Start-Process -FilePath $dnsSd.Source -ArgumentList @("-B", "_mcos._tcp", "local") -NoNewWindow -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        if (-not $process.WaitForExit($DiscoveryTimeoutSec * 1000)) {
            try { $process.Kill() } catch { }
        }
        $output = ((Get-Content -LiteralPath $stdout -Raw -ErrorAction SilentlyContinue) + "`n" + (Get-Content -LiteralPath $stderr -Raw -ErrorAction SilentlyContinue)).Trim()
        $passed = ($output -match "_mcos\._tcp" -or $output -match [regex]::Escape($target))
        $result = if ($passed) { "PASS" } else { "WARN" }
        $detail = if ($passed) { "dns-sd observed _mcos._tcp browse output." } else { "dns-sd ran, but no MCOS advertisement was observed in the capture window." }
        Add-Probe -Id "dns-sd" -Title "DNS-SD/mDNS browse" -Result $result -Detail $detail -Evidence $output
        return [pscustomobject][ordered]@{
            attempted = $true
            available = $true
            passed = $passed
            command = "dns-sd -B _mcos._tcp local"
            output = $output
            reasonIfUnavailable = ""
        }
    } catch {
        $message = $_.Exception.Message
        Add-Probe -Id "dns-sd" -Title "DNS-SD/mDNS browse" -Result "WARN" -Detail "dns-sd failed: $message"
        return [pscustomobject][ordered]@{
            attempted = $true
            available = $true
            passed = $false
            command = "dns-sd -B _mcos._tcp local"
            output = ""
            reasonIfUnavailable = $message
        }
    } finally {
        if ($process -and -not $process.HasExited) { try { $process.Kill() } catch { } }
        if (Test-Path $stdout) { Remove-Item -LiteralPath $stdout -Force -ErrorAction SilentlyContinue }
        if (Test-Path $stderr) { Remove-Item -LiteralPath $stderr -Force -ErrorAction SilentlyContinue }
    }
}

function Receive-UdpBeacon {
    param([int]$Port)

    if ($Port -le 0) {
        Add-Probe -Id "udp-beacon" -Title "UDP beacon capture" -Result "SKIP" -Detail "Beacon port was not available from discovery and was not supplied."
        return [pscustomobject][ordered]@{
            attempted = $false
            available = $false
            passed = $false
            port = $Port
            remoteEndpoint = ""
            payload = $null
            reasonIfUnavailable = "Beacon port was not available from discovery and was not supplied."
        }
    }

    $client = $null
    try {
        $client = [System.Net.Sockets.UdpClient]::new($Port)
        $client.Client.ReceiveTimeout = $DiscoveryTimeoutSec * 1000
        $endpoint = [System.Net.IPEndPoint]::new([System.Net.IPAddress]::Any, 0)
        $bytes = $client.Receive([ref]$endpoint)
        $text = [System.Text.Encoding]::UTF8.GetString($bytes)
        $payload = $null
        try { $payload = $text | ConvertFrom-Json -ErrorAction Stop } catch { $payload = $text }
        $passed = ($text -match "MCOS" -or $text -match "Master Control")
        $result = if ($passed) { "PASS" } else { "WARN" }
        Add-Probe -Id "udp-beacon" -Title "UDP beacon capture" -Result $result -Detail "Received UDP payload from $($endpoint.ToString()) on port $Port." -Evidence $payload
        return [pscustomobject][ordered]@{
            attempted = $true
            available = $true
            passed = $passed
            port = $Port
            remoteEndpoint = $endpoint.ToString()
            payload = $payload
            reasonIfUnavailable = ""
        }
    } catch {
        $message = $_.Exception.Message
        Add-Probe -Id "udp-beacon" -Title "UDP beacon capture" -Result "WARN" -Detail "No UDP beacon captured on port $Port within $DiscoveryTimeoutSec seconds: $message"
        return [pscustomobject][ordered]@{
            attempted = $true
            available = $true
            passed = $false
            port = $Port
            remoteEndpoint = ""
            payload = $null
            reasonIfUnavailable = $message
        }
    } finally {
        if ($client) { $client.Close(); $client.Dispose() }
    }
}

function ConvertTo-RedactedJsonText {
    param(
        [Parameter(Mandatory = $true)][object]$InputObject,
        [int]$Depth = 14
    )
    $json = $InputObject | ConvertTo-Json -Depth $Depth
    $secretPropertyPattern = '(?i)("([^"]*(secret|token|password|apikey|api_key|accesskey|access_key)[^"]*)"\s*:\s*)"([^"\\]|\\.)*"'
    return [regex]::Replace($json, $secretPropertyPattern, '$1"<redacted>"')
}

Write-Host "MCOS LAN discovery peer probe (non-destructive)" -ForegroundColor Cyan
Write-Host "  Target:     $target"
Write-Host "  AdminPort:  $AdminPort"
Write-Host "  GatewayPort:$GatewayPort"
Write-Host ""

$wellKnown = Invoke-JsonHttpProbe -Id "well-known" -Path "/.well-known/mcos.json" -Title "GET /.well-known/mcos.json"
$apiDiscovery = Invoke-JsonHttpProbe -Id "api-discovery" -Path "/api/discovery" -Title "GET /api/discovery"
$gatewayStatus = Invoke-JsonHttpProbe -Id "gateway-status" -Path "/api/gateway/status" -Title "GET /api/gateway/status"
$clientsProbe = Invoke-JsonHttpProbe -Id "clients" -Path "/api/clients" -Title "GET /api/clients"

$clientCount = 0
if ($clientsProbe.passed -and $null -ne $clientsProbe.response) {
    $clientsProperty = Get-ObjectPropertyValue -InputObject $clientsProbe.response -Name "clients"
    if ($null -ne $clientsProperty) {
        $clientCount = @($clientsProperty).Count
    } else {
        $clientCount = @($clientsProbe.response).Count
    }
    if ($RequireClient -and $clientCount -lt 1) {
        Add-Probe -Id "real-client" -Title "Real LAN client present" -Result "FAIL" -Required $true -Detail "No client entry was present in /api/clients."
    } elseif ($clientCount -gt 0) {
        Add-Probe -Id "real-client" -Title "Real LAN client present" -Result "PASS" -Detail "$clientCount client record(s) returned by /api/clients."
    } else {
        Add-Probe -Id "real-client" -Title "Real LAN client present" -Result "WARN" -Detail "No client entry present yet. Connect a real LAN client before claiming Gate E client validation."
    }
}

$resolvedBeaconPort = $BeaconPort
if ($resolvedBeaconPort -le 0 -and $apiDiscovery.passed -and $apiDiscovery.response) {
    $beaconProperty = Get-ObjectPropertyValue -InputObject $apiDiscovery.response -Name "beacon"
    $beaconPortProperty = Get-ObjectPropertyValue -InputObject $beaconProperty -Name "port"
    if ($null -ne $beaconPortProperty) {
        $resolvedBeaconPort = [int]$beaconPortProperty
    }
}

$dnsSdEvidence = Invoke-DnsSdBrowse
$udpEvidence = Receive-UdpBeacon -Port $resolvedBeaconPort

$requiredFailures = @($probes | Where-Object { $_.required -and $_.result -eq "FAIL" })
$discoverySignalObserved = ($dnsSdEvidence.passed -or $udpEvidence.passed)
$overall = "PASS"
if ($requiredFailures.Count -gt 0) { $overall = "FAIL" }
if (-not $discoverySignalObserved) { $overall = "FAIL" }

$report = [pscustomobject][ordered]@{
    schema = "mcos.lan-discovery-peer.v1"
    generatedAt = (Get-Date).ToString("o")
    startedAt = $startedAt
    peerHost = $env:COMPUTERNAME
    peerUser = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
    powershellVersion = $PSVersionTable.PSVersion.ToString()
    targetHostOrAddress = $target
    adminPort = $AdminPort
    gatewayPort = $GatewayPort
    beaconPort = $resolvedBeaconPort
    strictMode = [bool]$Strict
    requireClient = [bool]$RequireClient
    nonDestructive = $true
    httpDiscovery = [pscustomobject][ordered]@{
        wellKnown = $wellKnown
        apiDiscovery = $apiDiscovery
        gatewayStatus = $gatewayStatus
        clients = $clientsProbe
    }
    dnsSdEvidence = $dnsSdEvidence
    udpBeaconEvidence = $udpEvidence
    realClientEvidence = [pscustomobject][ordered]@{
        required = [bool]$RequireClient
        present = ($clientCount -gt 0)
        count = $clientCount
    }
    discoverySignalObserved = $discoverySignalObserved
    overall = $overall
    redaction = [pscustomobject][ordered]@{
        secretsRedacted = $true
        tokensRedacted = $true
    }
    probes = $probes
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$defaultDir = Join-Path $repoRoot "artifacts\deployability-audit\lan-peer"
if ($OutputDirectory) {
    if (-not $ReportPath) { $ReportPath = Join-Path $OutputDirectory "lan-peer-discovery-report.json" }
    if (-not $SummaryPath) { $SummaryPath = Join-Path $OutputDirectory "lan-peer-discovery-summary.md" }
} else {
    if (-not $ReportPath) { $ReportPath = Join-Path $defaultDir "lan-peer-discovery-report.json" }
    if (-not $SummaryPath) { $SummaryPath = Join-Path $defaultDir "lan-peer-discovery-summary.md" }
}

$reportDir = Split-Path -Parent $ReportPath
if ($reportDir -and -not (Test-Path $reportDir)) { New-Item -ItemType Directory -Force -Path $reportDir | Out-Null }
$summaryDir = Split-Path -Parent $SummaryPath
if ($summaryDir -and -not (Test-Path $summaryDir)) { New-Item -ItemType Directory -Force -Path $summaryDir | Out-Null }

Set-Content -Path $ReportPath -Value (ConvertTo-RedactedJsonText -InputObject $report -Depth 14) -Encoding UTF8

$md = New-Object System.Text.StringBuilder
[void]$md.AppendLine("# MCOS LAN Discovery Peer Evidence")
[void]$md.AppendLine("")
[void]$md.AppendLine("- Peer host: ``$($report.peerHost)``")
[void]$md.AppendLine("- Target: ``$target``")
[void]$md.AppendLine("- Generated: $($report.generatedAt)")
[void]$md.AppendLine("- Admin port: ``$AdminPort``")
[void]$md.AppendLine("- Gateway port: ``$GatewayPort``")
[void]$md.AppendLine("- Beacon port: ``$resolvedBeaconPort``")
[void]$md.AppendLine("- Overall: **$overall**")
[void]$md.AppendLine("- Discovery signal observed: **$discoverySignalObserved**")
[void]$md.AppendLine("")
[void]$md.AppendLine("| Result | Check | Detail |")
[void]$md.AppendLine("|---|---|---|")
foreach ($probe in $probes) {
    $detail = ($probe.detail -replace '\|', '\|')
    [void]$md.AppendLine("| $($probe.result) | $($probe.title) | $detail |")
}
Set-Content -Path $SummaryPath -Value ($md.ToString()) -Encoding UTF8

Write-Host ""
Write-Host "Overall: $overall" -ForegroundColor Cyan
Write-Host "JSON report:     $ReportPath"
Write-Host "Markdown report: $SummaryPath"

if ($Strict -and $overall -ne "PASS") { exit 1 }
exit 0
