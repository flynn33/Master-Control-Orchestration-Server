# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.

<#
.SYNOPSIS
    Generate a portable MCOS human alpha test kit.

.DESCRIPTION
    Produces a self-contained folder an operator can hand to a second LAN host
    with nothing but PowerShell installed -- no Git, no Visual Studio, no
    repository clone. The kit contains:

      LOCAL-HOST-TEST-CARD.md      operator card for the MCOS host
      SECOND-HOST-TEST-CARD.md     operator card for the LAN peer
      Run-LocalAcceptance.ps1      wrapper over the acceptance orchestrator
      Run-LanPeerAcceptance.ps1    fully self-contained second-host sequence
      Invoke-MasterControlWorkingAlphaAcceptance.ps1  bundled orchestrator (local)
      MasterControlAcceptanceCommon.ps1               bundled helper (local)
      client-bundle-template.json  onboarding bundle template (or generated)
      EXPECTED-OUTPUT.md           what a passing run looks like
      evidence\                    output layout for the run

    Run-LanPeerAcceptance.ps1 inlines its own HTTP/JSON-RPC/routability/evidence
    helpers so it never dot-sources a repository module; the peer needs only
    PowerShell and a network path to the MCOS host.

.NOTES
    Requires PowerShell 7+ for the acceptance runs (the peer script and the
    orchestrator use Invoke-WebRequest -SkipHttpErrorCheck).
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$BaseUrl = 'http://localhost:7300',
    [string]$InstallDirectory,
    [string]$BootstrapperPath,
    [string]$ClientBundlePath,
    [string]$ClientId = 'human-alpha-peer-01'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$kitRoot = New-Item -ItemType Directory -Force -Path $OutputDirectory | Select-Object -ExpandProperty FullName
$evidenceDir = New-Item -ItemType Directory -Force -Path (Join-Path $kitRoot 'evidence') | Select-Object -ExpandProperty FullName
Write-Host "Generating MCOS human alpha test kit at: $kitRoot"

# Bundle the orchestrator + shared helper next to this generator so the local
# card runs without a repository checkout.
$here = $PSScriptRoot
foreach ($f in @('Invoke-MasterControlWorkingAlphaAcceptance.ps1', 'MasterControlAcceptanceCommon.ps1')) {
    $srcF = Join-Path $here $f
    if (Test-Path -LiteralPath $srcF) {
        Copy-Item -LiteralPath $srcF -Destination (Join-Path $kitRoot $f) -Force
    }
    else {
        Write-Warning "Bundled dependency not found next to generator: $f"
    }
}

# ---------------------------------------------------------------------------
# LOCAL-HOST-TEST-CARD.md
# ---------------------------------------------------------------------------
$localCard = @'
# MCOS Local Host Human Test Card

## Purpose

Prove that MCOS is installed, service-running, locally healthy, and ready for
LAN peer validation.

## Non-mutating inspection

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Run-LocalAcceptance.ps1 `
  -Mode Inspect `
  -AdminBaseUrl http://localhost:7300 `
  -OutputDirectory .\mcos-local-evidence `
  -ZipEvidence
```

## Operator-authorized local certification

Run only after the operator authorizes the relevant mutating gates. Each gate
that is not authorized is reported NotRunMissingAuthorization (never a pass).

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Run-LocalAcceptance.ps1 `
  -Mode CertifyLocal `
  -AdminBaseUrl http://localhost:7300 `
  -MsiPath <path-to-msi> `
  -AuthorizeInstall `
  -AuthorizeServiceMutation `
  -AuthorizeFirewallMutation `
  -AuthorizeUrlAclMutation `
  -OperatorName "<name>" -OperatorRole "<role>" `
  -OutputDirectory .\mcos-local-certification-evidence `
  -ZipEvidence
```

## Human UI checks

1. Open the browser dashboard from the local host.
2. Verify the Working-Alpha Readiness, gateway, pools, clients, and diagnostics
   sections render live state (and show "unavailable" honestly if the API is down).
3. Launch the WinUI maintainer shell if installed.
4. Verify the Overview readiness card, diagnostics/gateway/pools/clients views load.
5. Record screenshots or notes for any failure in ui-human-notes.md.

## Expected pass results

- Service exists and runs.
- Admin API + gateway JSON-RPC probes pass.
- Safe MCP tool call returns the validated result.
- Every required pool shows live worker readiness (not template-only).
- Diagnostics export succeeds.
- Working-alpha readiness never reports ready without live evidence.
- Evidence bundle (acceptance-report.json + ACCEPTANCE-SUMMARY.md) is created.
'@
Set-Content -LiteralPath (Join-Path $kitRoot 'LOCAL-HOST-TEST-CARD.md') -Value $localCard -Encoding UTF8

# ---------------------------------------------------------------------------
# SECOND-HOST-TEST-CARD.md
# ---------------------------------------------------------------------------
$secondCard = @'
# MCOS Second-Host Human Test Card

## Purpose

Prove that MCOS is reachable and usable from a separate LAN host, not only from
localhost. This card needs only PowerShell on the peer -- no Git, Visual Studio,
or repository clone.

## Prerequisites

- MCOS installed and running on the target Windows host.
- LAN peer on the same trusted Private/Domain network.
- PowerShell 7+ on the LAN peer.
- Admin base URL (http://<mcos-host>:7300) or the generated client-bundle.json.

## Run

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Run-LanPeerAcceptance.ps1 `
  -AdminBaseUrl http://<mcos-host>:7300 `
  -ClientId human-alpha-peer-01 `
  -OutputDirectory .\mcos-lan-peer-evidence `
  -ZipEvidence
```

Or with a generated bundle from the MCOS host:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\Run-LanPeerAcceptance.ps1 `
  -ClientBundlePath .\client-bundle.json `
  -OutputDirectory .\mcos-lan-peer-evidence `
  -ZipEvidence
```

## Expected pass results

- Well-known + API discovery fetches pass.
- Advertised URLs are LAN-routable, not loopback/wildcard.
- Gateway status and health pass.
- Client registration/update passes.
- Client + telemetry heartbeats pass.
- MCP initialize / ping / tools/list pass.
- MCP safe tools/call returns the validated content.
- Client roster shows fresh liveness within the freshness window.
- Evidence bundle (lan-peer-report.json + LAN-PEER-SUMMARY.md) is created.

## Fail conditions

Fail the alpha if any required check fails. Do not accept a manual explanation
as a replacement for evidence.
'@
Set-Content -LiteralPath (Join-Path $kitRoot 'SECOND-HOST-TEST-CARD.md') -Value $secondCard -Encoding UTF8

# ---------------------------------------------------------------------------
# Run-LocalAcceptance.ps1 (wrapper over the bundled orchestrator)
# ---------------------------------------------------------------------------
$runLocal = @'
# Master Control Orchestration Server - local acceptance wrapper (generated kit).
# Copyright (c) 2026 James Daley. All Rights Reserved.
[CmdletBinding()]
param(
    [ValidateSet('Inspect', 'CertifyLocal', 'FullCertification')][string]$Mode = 'Inspect',
    [string]$AdminBaseUrl = 'http://localhost:7300',
    [string]$GatewayUrl,
    [string]$OutputDirectory = './mcos-local-evidence',
    [string]$InstallDirectory,
    [string]$BootstrapperPath,
    [string]$MsiPath,
    [string]$LanPeerReportPath,
    [switch]$ZipEvidence,
    [switch]$Json,
    [string]$OperatorName,
    [string]$OperatorRole,
    [switch]$AuthorizeInstall,
    [switch]$AuthorizeServiceMutation,
    [switch]$AuthorizeFirewallMutation,
    [switch]$AuthorizeUrlAclMutation,
    [switch]$AuthorizeTlsMutation,
    [switch]$AllowRepairTest,
    [switch]$AuthorizeUninstall,
    [switch]$AllowReinstallTest,
    [switch]$AuthorizeSecondHostLanTest
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$orchestrator = Join-Path $PSScriptRoot 'Invoke-MasterControlWorkingAlphaAcceptance.ps1'
if (-not (Test-Path -LiteralPath $orchestrator)) {
    Write-Error "Bundled orchestrator not found: $orchestrator"
    exit 3
}
# -Json is a compatibility no-op: the orchestrator always writes JSON.
$forward = @{
    Mode         = $Mode
    BaseUrl      = $AdminBaseUrl
    OutDirectory = $OutputDirectory
}
if ($GatewayUrl) { $forward.GatewayUrl = $GatewayUrl }
if ($InstallDirectory) { $forward.InstallDirectory = $InstallDirectory }
if ($BootstrapperPath) { $forward.BootstrapperPath = $BootstrapperPath }
if ($MsiPath) { $forward.MsiPath = $MsiPath }
if ($LanPeerReportPath) { $forward.LanPeerReportPath = $LanPeerReportPath }
if ($OperatorName) { $forward.OperatorName = $OperatorName }
if ($OperatorRole) { $forward.OperatorRole = $OperatorRole }
if ($ZipEvidence) { $forward.ZipEvidence = $true }
foreach ($sw in 'AuthorizeInstall', 'AuthorizeServiceMutation', 'AuthorizeFirewallMutation',
    'AuthorizeUrlAclMutation', 'AuthorizeTlsMutation', 'AllowRepairTest', 'AuthorizeUninstall',
    'AllowReinstallTest', 'AuthorizeSecondHostLanTest') {
    if ($PSBoundParameters.ContainsKey($sw) -and $PSBoundParameters[$sw]) { $forward[$sw] = $true }
}
# Forward only parameters the bundled orchestrator actually declares, so a kit
# built against an older/newer orchestrator degrades gracefully.
$valid = (Get-Command $orchestrator).Parameters.Keys
$call = @{}
foreach ($k in $forward.Keys) { if ($valid -contains $k) { $call[$k] = $forward[$k] } }
& $orchestrator @call
exit $LASTEXITCODE
'@
Set-Content -LiteralPath (Join-Path $kitRoot 'Run-LocalAcceptance.ps1') -Value $runLocal -Encoding UTF8

# ---------------------------------------------------------------------------
# Run-LanPeerAcceptance.ps1 (SELF-CONTAINED second-host sequence)
# ---------------------------------------------------------------------------
$runPeer = @'
# Master Control Orchestration Server - second-host LAN acceptance (generated kit).
# Copyright (c) 2026 James Daley. All Rights Reserved.
#
# Fully self-contained: inlines HTTP/JSON-RPC/routability/evidence helpers so the
# LAN peer needs only PowerShell 7+ and a network path to the MCOS host. It does
# NOT dot-source any repository module.
[CmdletBinding()]
param(
    [string]$AdminBaseUrl,
    [string]$GatewayUrl,
    [string]$ClientBundlePath,
    [string]$ClientId = 'human-alpha-peer-01',
    [string]$OutputDirectory = './mcos-lan-peer-evidence',
    [switch]$ZipEvidence,
    [switch]$Json,
    [int]$FreshnessSeconds = 120
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# --- resolve URLs from a bundle when provided ---
if ($ClientBundlePath) {
    if (-not (Test-Path -LiteralPath $ClientBundlePath)) { Write-Error "Client bundle not found: $ClientBundlePath"; exit 3 }
    $bundle = Get-Content -LiteralPath $ClientBundlePath -Raw | ConvertFrom-Json
    if (-not $AdminBaseUrl -and $bundle.PSObject.Properties['adminBaseUrl']) { $AdminBaseUrl = "$($bundle.adminBaseUrl)" }
    if (-not $GatewayUrl -and $bundle.PSObject.Properties['gatewayUrl']) { $GatewayUrl = "$($bundle.gatewayUrl)" }
    if ($bundle.PSObject.Properties['clientId'] -and $bundle.clientId) { $ClientId = "$($bundle.clientId)" }
}
if (-not $AdminBaseUrl) { Write-Error 'Provide -AdminBaseUrl or -ClientBundlePath.'; exit 3 }
$AdminBaseUrl = $AdminBaseUrl.TrimEnd('/')

$evidenceDir = New-Item -ItemType Directory -Force -Path (Join-Path $OutputDirectory 'evidence') | Select-Object -ExpandProperty FullName
$checks = [System.Collections.Generic.List[object]]::new()
$evidenceFiles = [System.Collections.Generic.List[string]]::new()

function Get-Utc { [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ') }

function Invoke-Probe {
    param([string]$Method = 'GET', [string]$Url, [string]$Body, [hashtable]$Headers, [int]$TimeoutSec = 20)
    $r = [ordered]@{ method = $Method; url = $Url; statusCode = 0; ok = $false; jsonValid = $false; body = ''; json = $null; error = '' }
    if (-not $Url) { $r.error = 'missing url'; return $r }
    try {
        $p = @{ Method = $Method; Uri = $Url; TimeoutSec = $TimeoutSec; SkipHttpErrorCheck = $true; MaximumRedirection = 0; ErrorAction = 'Stop' }
        if ($Headers) { $p.Headers = $Headers }
        if ($Body) { $p.Body = $Body; $p.ContentType = 'application/json' }
        $resp = Invoke-WebRequest @p
        $r.statusCode = [int]$resp.StatusCode
        $r.body = "$($resp.Content)"
        $r.ok = ($r.statusCode -ge 200 -and $r.statusCode -lt 300)
        if ($r.body) { try { $r.json = $r.body | ConvertFrom-Json -ErrorAction Stop; $r.jsonValid = $true } catch { } }
    }
    catch { $r.error = $_.Exception.Message }
    return $r
}

function Invoke-Rpc {
    param([string]$Url, [string]$RpcMethod, $RpcParams, [int]$Id = 1)
    $req = [ordered]@{ jsonrpc = '2.0'; id = $Id; method = $RpcMethod }
    if ($null -ne $RpcParams) { $req.params = $RpcParams }
    $body = $req | ConvertTo-Json -Depth 12 -Compress
    return Invoke-Probe -Method POST -Url $Url -Body $body -Headers @{ 'Accept' = 'application/json, text/event-stream' }
}

function Get-JsonPath {
    param($Json, [string]$Path)
    $cur = $Json
    foreach ($seg in $Path.Split('.')) {
        if ($null -eq $cur) { return $null }
        if ($cur.PSObject -and $cur.PSObject.Properties[$seg]) { $cur = $cur.PSObject.Properties[$seg].Value } else { return $null }
    }
    return $cur
}

function Test-Routable {
    param([string]$Url)
    if (-not $Url) { return [ordered]@{ routable = $false; reason = 'No URL advertised.' } }
    try { $h = ([System.Uri]$Url).Host } catch { $h = ($Url -replace '^[a-z]+://', '') -replace '[:/].*$', '' }
    $h = $h.Trim('[', ']').ToLowerInvariant()
    if ($h -in @('0.0.0.0', '::', '*', '')) { return [ordered]@{ routable = $false; reason = "Advertised host '$h' is a wildcard bind." } }
    if ($h -eq 'localhost' -or $h -eq '::1' -or $h -like '127.*') { return [ordered]@{ routable = $false; reason = "Advertised host '$h' is loopback." } }
    return [ordered]@{ routable = $true; reason = "Advertised host '$h' is routable." }
}

function Write-Evidence {
    param([string]$Name, $Probe)
    $safe = ($Name -replace '[^\w\.\-]', '_')
    $meta = [ordered]@{ method = $Probe.method; url = $Probe.url; statusCode = $Probe.statusCode; ok = $Probe.ok; jsonValid = $Probe.jsonValid; error = $Probe.error }
    $meta | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $evidenceDir "$safe.meta.json") -Encoding UTF8
    if ($Probe.body) { Set-Content -LiteralPath (Join-Path $evidenceDir "$safe.body.txt") -Value $Probe.body -Encoding UTF8 }
    $evidenceFiles.Add("evidence/$safe.meta.json") | Out-Null
    return $safe
}

function Add-Check {
    param([string]$Id, [string]$Title, [ValidateSet('Pass', 'Fail', 'Warning', 'SkippedOptional')][string]$Status, [bool]$Required = $true, [string]$Detail = '', [string]$Evidence = '')
    $checks.Add([ordered]@{ id = $Id; title = $Title; required = $Required; status = $Status; timestampUtc = (Get-Utc); detail = $Detail; evidence = $Evidence }) | Out-Null
}

function Test-Endpoint {
    param([string]$Id, [string]$Title, [string]$Method = 'GET', [string]$Url, [string]$Body, [hashtable]$Headers, [string[]]$Fields, [int[]]$Accept = @(200), [bool]$Required = $true)
    $p = Invoke-Probe -Method $Method -Url $Url -Body $Body -Headers $Headers
    $ev = Write-Evidence -Name $Id -Probe $p
    $status = 'Pass'; $detail = ''
    if ($Accept -notcontains $p.statusCode) { $status = if ($Required) { 'Fail' } else { 'Warning' }; $detail = if ($p.error) { $p.error } else { "status $($p.statusCode)" } }
    elseif ($Fields) { foreach ($f in $Fields) { if ($null -eq (Get-JsonPath -Json $p.json -Path $f)) { $status = if ($Required) { 'Fail' } else { 'Warning' }; $detail = "missing $f" } } }
    Add-Check -Id $Id -Title $Title -Status $status -Required $Required -Detail $detail -Evidence $ev
    return $p
}

Write-Host "MCOS LAN peer acceptance | admin=$AdminBaseUrl | client=$ClientId"

# 1. Discovery
$wk = Test-Endpoint -Id 'well-known' -Title 'GET /.well-known/mcos.json' -Url "$AdminBaseUrl/.well-known/mcos.json"
$disc = Test-Endpoint -Id 'api-discovery' -Title 'GET /api/discovery' -Url "$AdminBaseUrl/api/discovery"
$advertised = if ($disc.jsonValid) { Get-JsonPath -Json $disc.json -Path 'gateway.mcpUrl' } else { $null }
$rt = Test-Routable -Url "$advertised"
Add-Check -Id 'discovery-routable' -Title 'Advertised gateway URL is LAN-routable' -Status ($(if ($rt.routable) { 'Pass' } else { 'Fail' })) -Detail $rt.reason
if (-not $GatewayUrl) { $GatewayUrl = if ($advertised) { "$advertised" } else { "$AdminBaseUrl" -replace ':7300', ':8080' } }
if (-not ($GatewayUrl -match '/mcp$')) { $GatewayUrl = "$($GatewayUrl.TrimEnd('/'))/mcp" }

# 2. Gateway
Test-Endpoint -Id 'gateway-status' -Title 'GET /api/gateway/status' -Url "$AdminBaseUrl/api/gateway/status" -Fields @('state') | Out-Null
Test-Endpoint -Id 'gateway-health' -Title 'GET /api/gateway/health' -Url "$AdminBaseUrl/api/gateway/health" -Fields @('status') | Out-Null

# 3. Client registration + heartbeat freshness
$reg = Invoke-Probe -Method POST -Url "$AdminBaseUrl/api/clients" -Body ((@{ clientId = $ClientId; clientType = 'codex' } | ConvertTo-Json -Compress))
$evReg = Write-Evidence -Name 'client-register' -Probe $reg
Add-Check -Id 'client-register' -Title 'POST /api/clients registers the peer' -Status ($(if ($reg.ok) { 'Pass' } else { 'Fail' })) -Detail "status $($reg.statusCode)" -Evidence $evReg
$hbHeaders = @{ 'X-MCOS-Client-Id' = $ClientId }
$hb = Invoke-Probe -Method POST -Url "$AdminBaseUrl/api/telemetry/heartbeat" -Body ((@{ clientId = $ClientId } | ConvertTo-Json -Compress)) -Headers $hbHeaders
$evHb = Write-Evidence -Name 'telemetry-heartbeat' -Probe $hb
Add-Check -Id 'telemetry-heartbeat' -Title 'POST /api/telemetry/heartbeat' -Status ($(if ($hb.ok) { 'Pass' } else { 'Fail' })) -Detail "status $($hb.statusCode)" -Evidence $evHb

# 4. MCP JSON-RPC
$init = Invoke-Rpc -Url $GatewayUrl -RpcMethod 'initialize' -RpcParams @{ protocolVersion = '2025-03-26'; capabilities = @{}; clientInfo = @{ name = 'mcos-lan-peer'; version = '1.0' } } -Id 1
$evInit = Write-Evidence -Name 'mcp-initialize' -Probe $init
$initOk = ($init.ok -and $init.jsonValid -and (Get-JsonPath -Json $init.json -Path 'result.protocolVersion'))
Add-Check -Id 'mcp-initialize' -Title 'MCP initialize' -Status ($(if ($initOk) { 'Pass' } else { 'Fail' })) -Detail "status $($init.statusCode)" -Evidence $evInit
$ping = Invoke-Rpc -Url $GatewayUrl -RpcMethod 'ping' -Id 2
$evPing = Write-Evidence -Name 'mcp-ping' -Probe $ping
Add-Check -Id 'mcp-ping' -Title 'MCP ping' -Status ($(if ($ping.ok) { 'Pass' } else { 'Fail' })) -Detail "status $($ping.statusCode)" -Evidence $evPing
$list = Invoke-Rpc -Url $GatewayUrl -RpcMethod 'tools/list' -Id 3
$evList = Write-Evidence -Name 'mcp-tools-list' -Probe $list
$tools = if ($list.jsonValid) { Get-JsonPath -Json $list.json -Path 'result.tools' } else { $null }
$toolNames = @(); if ($tools) { $toolNames = @($tools | ForEach-Object { "$($_.name)" }) }
Add-Check -Id 'mcp-tools-list' -Title 'MCP tools/list returns a catalog' -Status ($(if ($toolNames.Count -gt 0) { 'Pass' } else { 'Fail' })) -Detail "$($toolNames.Count) tools" -Evidence $evList
$addTool = $toolNames | Where-Object { $_ -match 'mcos\.add$' } | Select-Object -First 1
if ($addTool) {
    $call = Invoke-Rpc -Url $GatewayUrl -RpcMethod 'tools/call' -RpcParams @{ name = $addTool; arguments = @{ a = 2; b = 3 } } -Id 4
    $evCall = Write-Evidence -Name 'mcp-tools-call' -Probe $call
    $text = if ($call.jsonValid) { $c = Get-JsonPath -Json $call.json -Path 'result.content'; if ($c -and @($c).Count -gt 0) { "$(@($c)[0].text)" } else { '' } } else { '' }
    $callOk = ($call.ok -and $text.Trim() -eq '5')
    Add-Check -Id 'mcp-tools-call' -Title "MCP tools/call $addTool (2+3=5)" -Status ($(if ($callOk) { 'Pass' } else { 'Fail' })) -Detail "content='$text'" -Evidence $evCall
}
else {
    Add-Check -Id 'mcp-tools-call' -Title 'MCP safe tools/call' -Status 'Fail' -Detail 'no mcos.add tool advertised'
}

# 5. Roster freshness
$roster = Invoke-Probe -Method GET -Url "$AdminBaseUrl/api/telemetry/clients"
$evRoster = Write-Evidence -Name 'telemetry-clients' -Probe $roster
$fresh = $false
if ($roster.jsonValid) {
    $entry = @($roster.json) | Where-Object { "$($_.clientId)" -eq $ClientId } | Select-Object -First 1
    if ($entry) { $fresh = $true }
}
Add-Check -Id 'roster-freshness' -Title 'Peer appears with fresh liveness' -Status ($(if ($fresh) { 'Pass' } else { 'Fail' })) -Detail "clientId=$ClientId" -Evidence $evRoster

# --- report ---
$requiredFail = @($checks | Where-Object { $_.required -and $_.status -eq 'Fail' }).Count
$passed = ($requiredFail -eq 0)
$report = [ordered]@{
    schemaVersion = '1.0'
    product       = 'Master Control Orchestration Server'
    role          = 'lan-peer'
    passed        = $passed
    timestampUtc  = (Get-Utc)
    adminBaseUrl  = $AdminBaseUrl
    gatewayUrl    = $GatewayUrl
    clientId      = $ClientId
    checks        = @($checks)
    evidenceFiles = @($evidenceFiles | Select-Object -Unique)
}
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'lan-peer-report.json') -Encoding UTF8
$md = [System.Collections.Generic.List[string]]::new()
$md.Add('# MCOS LAN Peer Acceptance Summary'); $md.Add('')
$md.Add("Result: **$(if ($passed) { 'PASS' } else { 'FAIL' })**  ·  admin: $AdminBaseUrl  ·  $((Get-Utc))"); $md.Add('')
$md.Add('| Check | Required | Status | Detail |'); $md.Add('|---|---|---|---|')
foreach ($c in $checks) { $md.Add("| $($c.id) | $(if ($c.required) { 'yes' } else { 'no' }) | $($c.status) | $(("$($c.detail)") -replace '\|', '\|') |") }
($md -join [Environment]::NewLine) | Set-Content -LiteralPath (Join-Path $OutputDirectory 'LAN-PEER-SUMMARY.md') -Encoding UTF8

if ($ZipEvidence) {
    $zip = Join-Path $OutputDirectory 'mcos-lan-peer-evidence.zip'
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    Compress-Archive -Path (Join-Path $OutputDirectory '*') -DestinationPath $zip -Force
}
Write-Host "Result: $(if ($passed) { 'PASS' } else { 'FAIL' })  (report: $(Join-Path $OutputDirectory 'lan-peer-report.json'))"
if (-not $passed) { exit 1 }
exit 0
'@
Set-Content -LiteralPath (Join-Path $kitRoot 'Run-LanPeerAcceptance.ps1') -Value $runPeer -Encoding UTF8

# ---------------------------------------------------------------------------
# client-bundle-template.json (or copy the provided bundle)
# ---------------------------------------------------------------------------
if ($ClientBundlePath -and (Test-Path -LiteralPath $ClientBundlePath)) {
    Copy-Item -LiteralPath $ClientBundlePath -Destination (Join-Path $kitRoot 'client-bundle.json') -Force
    Write-Host "Included client bundle: client-bundle.json"
}
else {
    $bundleTemplate = [ordered]@{
        schemaVersion = '1.0'
        clientId      = $ClientId
        clientType    = 'codex'
        adminBaseUrl  = $BaseUrl.TrimEnd('/')
        gatewayUrl    = "$($BaseUrl.TrimEnd('/'))/mcp"
        discoveryUrl  = "$($BaseUrl.TrimEnd('/'))/.well-known/mcos.json"
        onboardingUrl = "$($BaseUrl.TrimEnd('/'))/api/onboarding"
        heartbeatUrl  = "$($BaseUrl.TrimEnd('/'))/api/telemetry/heartbeat"
        identityHeader = 'X-MCOS-Client-Id'
        note          = 'Template. Replace adminBaseUrl host with the MCOS host LAN address before use on a peer.'
    }
    $bundleTemplate | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $kitRoot 'client-bundle-template.json') -Encoding UTF8
}

# ---------------------------------------------------------------------------
# EXPECTED-OUTPUT.md + README.md + evidence layout marker
# ---------------------------------------------------------------------------
$expected = @'
# Expected Output

A passing run produces, in the chosen output directory:

- `acceptance-report.json` (local) or `lan-peer-report.json` (peer) - machine-readable checks.
- `ACCEPTANCE-SUMMARY.md` (local) or `LAN-PEER-SUMMARY.md` (peer) - human-readable table.
- `evidence/<check>.meta.json` + `<check>.body.txt` - per-probe request/response.
- `mcos-working-alpha-evidence.zip` / `mcos-lan-peer-evidence.zip` when `-ZipEvidence` is passed.

A passing local run classifies as `DeployableHumanTestableWorkingAlpha` only
when every required check passes AND every required mutating gate was authorized
and passed. Otherwise it classifies as `ReadyForOperatorAuthorizedCertification`
(runtime good, gates pending) or `NotCertified` (a required check failed).

Working-alpha readiness never reports ready without live evidence; a not-ready
report is honest, not a script failure.
'@
Set-Content -LiteralPath (Join-Path $kitRoot 'EXPECTED-OUTPUT.md') -Value $expected -Encoding UTF8

$readme = @'
# MCOS Human Alpha Test Kit

Portable kit for validating a running MCOS working alpha.

- On the MCOS host: read LOCAL-HOST-TEST-CARD.md, run Run-LocalAcceptance.ps1.
- On a LAN peer (PowerShell only, no repo needed): read SECOND-HOST-TEST-CARD.md,
  copy this folder over, run Run-LanPeerAcceptance.ps1.

Run-LanPeerAcceptance.ps1 is fully self-contained and dot-sources nothing.
'@
Set-Content -LiteralPath (Join-Path $kitRoot 'README.md') -Value $readme -Encoding UTF8

Set-Content -LiteralPath (Join-Path $evidenceDir '.keep') -Value 'Evidence files are written here by the acceptance runs.' -Encoding UTF8

Write-Host "Kit generated. Files:"
Get-ChildItem -LiteralPath $kitRoot -Name | ForEach-Object { Write-Host "  $_" }
