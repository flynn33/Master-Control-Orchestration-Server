# Master Control Orchestration Server
# Copyright (c) 2026 James Daley. All Rights Reserved.
# Proprietary and Confidential.
#
# Shared helpers for the MCOS working-alpha acceptance scripts:
#   - Test-MasterControlOrchestrationServerWorkingAlpha.ps1 (local runtime)
#   - Register-MasterControlLanClient.ps1                   (client onboarding)
#   - Test-MasterControlLanClientAcceptance.ps1             (second-host LAN)
#
# Dot-source this file (mirrors the Resolve-MasterControlToolchain.ps1
# precedent):
#     . (Join-Path $PSScriptRoot 'MasterControlAcceptanceCommon.ps1')
#
# Targets PowerShell 7+ (the acceptance gates invoke `pwsh`). All probes are
# non-mutating GET/POST HTTP calls; no host state is changed by this module.

Set-StrictMode -Version Latest

# ---------------------------------------------------------------------------
# HTTP probing
# ---------------------------------------------------------------------------

# Perform a single HTTP probe and return a structured result WITHOUT throwing
# on non-2xx status. -SkipHttpErrorCheck (PowerShell 7.4+) lets us capture the
# real status code and body for evidence even on 4xx/5xx.
function Invoke-McosHttpProbe {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][ValidateSet('GET', 'POST', 'PUT', 'DELETE')][string]$Method,
        [string]$Url,
        [string]$Body,
        [hashtable]$Headers,
        [int]$TimeoutSec = 15
    )

    $result = [ordered]@{
        method      = $Method
        url         = $Url
        statusCode  = 0
        contentType = ''
        ok          = $false
        jsonValid   = $false
        body        = ''
        json        = $null
        error       = ''
    }

    if ([string]::IsNullOrWhiteSpace($Url)) {
        $result.error = 'empty or missing URL'
        return $result
    }

    try {
        $params = @{
            Method             = $Method
            Uri                = $Url
            TimeoutSec         = $TimeoutSec
            MaximumRedirection = 0
            ErrorAction        = 'Stop'
            UseBasicParsing    = $true
        }
        # -SkipHttpErrorCheck exists only on PowerShell 7.4+. On Windows PowerShell 5.1
        # a non-2xx response throws; the catch block recovers the status/body instead.
        if ($PSVersionTable.PSVersion -ge [version]'7.4') { $params['SkipHttpErrorCheck'] = $true }
        if ($Headers) { $params['Headers'] = $Headers }
        if (-not [string]::IsNullOrEmpty($Body)) {
            $params['Body'] = $Body
            $params['ContentType'] = 'application/json'
        }

        $response = Invoke-WebRequest @params
        $result.statusCode = [int]$response.StatusCode
        try { $result.contentType = "$($response.Headers['Content-Type'])" } catch { $result.contentType = '' }
        $result.body = "$($response.Content)"
        $result.ok = ($result.statusCode -ge 200 -and $result.statusCode -lt 300)
        if (-not [string]::IsNullOrWhiteSpace($result.body)) {
            try {
                $result.json = $result.body | ConvertFrom-Json -ErrorAction Stop
                $result.jsonValid = $true
            } catch {
                $result.jsonValid = $false
            }
        }
    } catch {
        $result.error = $_.Exception.Message
        # On editions without -SkipHttpErrorCheck (Windows PowerShell 5.1) a non-2xx
        # response throws; recover the real status code and body from the response so
        # evidence probes report the actual HTTP status instead of 0.
        $resp = $null
        if ($_.Exception.PSObject.Properties['Response']) { $resp = $_.Exception.Response }
        if ($resp) {
            try { $result.statusCode = [int]$resp.StatusCode } catch {}
            try {
                $stream = $resp.GetResponseStream()
                if ($stream) {
                    $reader = New-Object System.IO.StreamReader($stream)
                    $result.body = $reader.ReadToEnd()
                    $reader.Close()
                }
            } catch {}
            $result.ok = ($result.statusCode -ge 200 -and $result.statusCode -lt 300)
            if (-not [string]::IsNullOrWhiteSpace($result.body)) {
                try { $result.json = $result.body | ConvertFrom-Json -ErrorAction Stop; $result.jsonValid = $true } catch { $result.jsonValid = $false }
            }
        }
    }

    return $result
}

# Walk a dotted JSON path (e.g. "result.serverInfo.name") over a parsed object.
# Returns $null when any segment is absent.
function Get-McosJsonPath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][AllowNull()]$Json,
        [Parameter(Mandatory)][string]$Path
    )
    $current = $Json
    foreach ($segment in $Path.Split('.')) {
        if ($null -eq $current) { return $null }
        if ($current -is [System.Collections.IDictionary]) {
            if ($current.Contains($segment)) { $current = $current[$segment] } else { return $null }
        } elseif ($current.PSObject -and $current.PSObject.Properties[$segment]) {
            $current = $current.PSObject.Properties[$segment].Value
        } else {
            return $null
        }
    }
    return $current
}

# Return the list of required (possibly dotted) fields that are absent from the
# parsed JSON. An empty list means every required field is present.
function Get-McosMissingFields {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][AllowNull()]$Json,
        [string[]]$RequiredFields
    )
    $missing = @()
    if ($null -eq $RequiredFields) { return , $missing }
    foreach ($field in $RequiredFields) {
        if ($null -eq (Get-McosJsonPath -Json $Json -Path $field)) {
            $missing += $field
        }
    }
    return , $missing
}

# ---------------------------------------------------------------------------
# MCP JSON-RPC
# ---------------------------------------------------------------------------

# POST a single JSON-RPC 2.0 request to the gateway MCP endpoint.
function Invoke-McosMcpRpc {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$GatewayUrl,
        [Parameter(Mandatory)][string]$RpcMethod,
        $RpcParams,
        [int]$Id = 1,
        [hashtable]$Headers,
        [int]$TimeoutSec = 20
    )
    $request = [ordered]@{ jsonrpc = '2.0'; id = $Id; method = $RpcMethod }
    if ($PSBoundParameters.ContainsKey('RpcParams') -and $null -ne $RpcParams) {
        $request['params'] = $RpcParams
    }
    $body = $request | ConvertTo-Json -Depth 12 -Compress
    $mergedHeaders = @{ 'Accept' = 'application/json, text/event-stream' }
    if ($Headers) { foreach ($k in $Headers.Keys) { $mergedHeaders[$k] = $Headers[$k] } }
    return Invoke-McosHttpProbe -Method POST -Url $GatewayUrl -Body $body -Headers $mergedHeaders -TimeoutSec $TimeoutSec
}

# ---------------------------------------------------------------------------
# Routability (posture-conditional)
# ---------------------------------------------------------------------------

$script:McosLoopbackHosts = @('localhost', '127.0.0.1', '::1', '[::1]')
$script:McosWildcardHosts = @('0.0.0.0', '::', '*', '')

function Get-McosUrlHost {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Url)
    try {
        $uri = [System.Uri]$Url
        $h = $uri.Host
    } catch {
        # Fall back to a coarse parse for non-.NET-parseable inputs.
        $h = ($Url -replace '^[a-zA-Z]+://', '') -replace '[:/].*$', ''
    }
    return ($h.Trim('[', ']')).ToLowerInvariant()
}

function Test-McosHostLoopback {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$HostName)
    $h = $HostName.ToLowerInvariant()
    if ($script:McosLoopbackHosts -contains $h) { return $true }
    return ($h -like '127.*')
}

function Test-McosHostWildcard {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$HostName)
    return ($script:McosWildcardHosts -contains $HostName.ToLowerInvariant())
}

# Decide whether an advertised URL is acceptable given posture.
#   -RequireLanRoutable : reject loopback/wildcard (second-host / trusted-LAN).
#   default             : loopback is acceptable (local-only posture).
# Returns an object { routable=<bool>; host=<string>; reason=<string> }.
function Test-McosUrlRoutable {
    [CmdletBinding()]
    param(
        [string]$Url,
        [switch]$RequireLanRoutable
    )
    if ([string]::IsNullOrWhiteSpace($Url)) {
        return [ordered]@{ routable = $false; host = ''; reason = 'No URL was advertised to evaluate.' }
    }
    $h = Get-McosUrlHost -Url $Url
    $isLoopback = Test-McosHostLoopback -HostName $h
    $isWildcard = Test-McosHostWildcard -HostName $h
    $routable = $true
    $reason = 'Advertised host is acceptable for the current posture.'
    if ($RequireLanRoutable) {
        if ($isWildcard) { $routable = $false; $reason = "Advertised host '$h' is a wildcard bind address; not LAN-routable." }
        elseif ($isLoopback) { $routable = $false; $reason = "Advertised host '$h' is loopback; not reachable from a second LAN host." }
    }
    return [ordered]@{ routable = $routable; host = $h; reason = $reason }
}

# ---------------------------------------------------------------------------
# Evidence + checklist + reports
# ---------------------------------------------------------------------------

function New-McosEvidenceDirectory {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Force -Path $Path | Out-Null
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

# Persist a probe's request/response pair for the evidence bundle. Returns the
# relative evidence file base name.
function Write-McosEvidence {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Directory,
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)]$Probe
    )
    $null = New-McosEvidenceDirectory -Path $Directory
    $safe = ($Name -replace '[^\w\.\-]', '_')
    $meta = [ordered]@{
        method      = $Probe.method
        url         = $Probe.url
        statusCode  = $Probe.statusCode
        contentType = $Probe.contentType
        ok          = $Probe.ok
        jsonValid   = $Probe.jsonValid
        error       = $Probe.error
    }
    $meta | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Directory "$safe.meta.json") -Encoding UTF8
    if (-not [string]::IsNullOrEmpty($Probe.body)) {
        Set-Content -LiteralPath (Join-Path $Directory "$safe.body.txt") -Value $Probe.body -Encoding UTF8
    }
    return $safe
}

function New-McosCheckList {
    # Comma-wrap so PowerShell does not enumerate the (initially empty) list
    # down to $null on assignment.
    return , ([System.Collections.Generic.List[object]]::new())
}

# Append a check result. Returns the passed flag so callers can accumulate.
function Add-McosCheck {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]$Checks,
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][bool]$Passed,
        [string]$Detail = '',
        [string]$Evidence = '',
        [bool]$Required = $true
    )
    $Checks.Add([ordered]@{
            name     = $Name
            passed   = $Passed
            required = $Required
            detail   = $Detail
            evidence = $Evidence
        }) | Out-Null
    return $Passed
}

# True when every REQUIRED check passed. Optional checks never fail the run.
function Test-McosChecksPassed {
    [CmdletBinding()]
    param([Parameter(Mandatory)]$Checks)
    foreach ($check in $Checks) {
        if ($check.required -and -not $check.passed) { return $false }
    }
    return $true
}

# Write a JSON report + a human-readable Markdown summary. Returns the JSON path.
function Write-McosReport {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Title,
        [Parameter(Mandatory)]$Checks,
        [Parameter(Mandatory)][string]$JsonPath,
        [string]$MarkdownPath,
        [hashtable]$Context
    )
    $passed = Test-McosChecksPassed -Checks $Checks
    $report = [ordered]@{
        title       = $Title
        passed      = $passed
        checks      = @($Checks)
        context     = $Context
        checkCount  = @($Checks).Count
        failedCount = @($Checks | Where-Object { $_.required -and -not $_.passed }).Count
    }
    $report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $JsonPath -Encoding UTF8

    if ($MarkdownPath) {
        $lines = [System.Collections.Generic.List[string]]::new()
        $lines.Add("# $Title")
        $lines.Add('')
        $lines.Add("Result: **$(if ($passed) { 'PASS' } else { 'FAIL' })**")
        $lines.Add('')
        $lines.Add('| Check | Required | Result | Detail |')
        $lines.Add('|---|---|---|---|')
        foreach ($check in $Checks) {
            $status = if ($check.passed) { 'pass' } elseif ($check.required) { 'FAIL' } else { 'optional' }
            $detail = ("$($check.detail)" -replace '\|', '\|')
            $req = if ($check.required) { 'yes' } else { 'no' }
            $lines.Add("| $($check.name) | $req | $status | $detail |")
        }
        ($lines -join [Environment]::NewLine) | Set-Content -LiteralPath $MarkdownPath -Encoding UTF8
    }

    return $JsonPath
}

# ---------------------------------------------------------------------------
# Install-state discovery (best-effort default for -BaseUrl)
# ---------------------------------------------------------------------------

# Resolve the admin base URL from an installed MCOS instance, when discoverable,
# by reading installation-state.json. Returns $null when not found.
function Resolve-McosAdminBaseUrlFromInstallState {
    [CmdletBinding()]
    param([string]$InstallDirectory)

    $candidates = @()
    if ($InstallDirectory) { $candidates += (Join-Path $InstallDirectory 'installation-state.json') }
    $candidates += @(
        (Join-Path ${env:ProgramFiles} 'Master Control Orchestration Server\installation-state.json')
    )
    foreach ($path in $candidates) {
        if ($path -and (Test-Path -LiteralPath $path)) {
            try {
                $state = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json -ErrorAction Stop
                if ($state.PSObject.Properties['browserUrl'] -and $state.browserUrl) {
                    return "$($state.browserUrl)".TrimEnd('/')
                }
                if ($state.PSObject.Properties['browserPort'] -and $state.browserPort) {
                    return "http://127.0.0.1:$($state.browserPort)"
                }
            } catch {
                # Unreadable state file: fall through to the next candidate.
            }
        }
    }
    return $null
}
