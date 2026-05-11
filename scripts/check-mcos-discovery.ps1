# MCOS Discovery / DNS-SD / UDP-beacon self-test
#
# Verifies the four discovery surfaces an external LAN AI client uses
# to find MCOS:
#
#   1. /.well-known/mcos.json   - canonical discovery document
#   2. /api/discovery           - same doc, with beacon-only metadata
#   3. /api/beacon              - the JSON the UDP beacon broadcasts
#   4. UDP/7301 (default)       - listens for a real beacon emission
#
# Plus a best-effort DNS-SD check via PowerShell DnsClient (if the host
# has Bonjour or a similar mDNSResponder running, _mcos._tcp.local
# should resolve).
#
# Exit code:
#   0  all checks pass
#   1  one or more checks failed (stderr-style report on stdout)
#
# Run: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-mcos-discovery.ps1
#
# Optional parameters:
#   -AdminUrl   default http://localhost:7300
#   -BeaconPort default 7301
#   -BeaconTimeoutSec default 18 (one full beacon broadcast interval
#                                  is 15s by default; +3s buffer ensures
#                                  we're within the next cycle even if
#                                  the test starts right after a sent
#                                  beacon)

[CmdletBinding()]
param(
    [string]$AdminUrl = "http://localhost:7300",
    [int]$BeaconPort = 7301,
    [int]$BeaconTimeoutSec = 18
)

$ErrorActionPreference = "Continue"
$ProgressPreference    = "SilentlyContinue"

# v0.9.6: HTTP client default timeout. Made long enough to absorb the
# first-call-after-service-start latency (PDH counter cold-init in
# telemetryService_->captureSnapshot can take 1-3s on a freshly-started
# MCOS; the discovery doc + every onboarding profile transitively depend
# on that snapshot). Pre-v0.9.6 the per-call timeout was 5s, which
# false-alarmed the self-test when run within ~10s of a service restart.
$script:HttpTimeoutSec = 20

$pass = 0
$fail = 0

function Check {
    param(
        [string]$Name,
        [scriptblock]$Body
    )
    Write-Host "[?] $Name" -NoNewline
    try {
        $msg = & $Body
        Write-Host "  PASS" -ForegroundColor Green
        if ($msg) { "    $msg" }
        $script:pass++
    } catch {
        Write-Host "  FAIL" -ForegroundColor Red
        "    $($_.Exception.Message)"
        $script:fail++
    }
}

Write-Host "MCOS discovery self-test" -ForegroundColor Cyan
Write-Host "  AdminUrl:          $AdminUrl"
Write-Host "  BeaconPort:        $BeaconPort"
Write-Host "  BeaconTimeoutSec:  $BeaconTimeoutSec"
Write-Host ""

# 1. /.well-known/mcos.json shape
Check "/.well-known/mcos.json returns valid discovery doc" {
    $wk = Invoke-RestMethod -Uri "$AdminUrl/.well-known/mcos.json" -TimeoutSec $script:HttpTimeoutSec
    if (-not $wk.product)               { throw "missing product" }
    if (-not $wk.role)                  { throw "missing role" }
    if (-not $wk.gateway.mcpUrl)        { throw "missing gateway.mcpUrl" }
    if (-not $wk.gateway.healthUrl)     { throw "missing gateway.healthUrl" }
    if ($wk.auth -ne "none")            { throw "auth != 'none' (got '$($wk.auth)')" }
    if ($wk.trust -ne "lan")            { throw "trust != 'lan' (got '$($wk.trust)')" }
    # v0.9.4: admin block + mcp-2025-03-26 capability
    if (-not $wk.admin.poolsUrl)        { throw "missing admin.poolsUrl (v0.9.4 regression)" }
    if (-not ($wk.capabilities -contains "mcp-2025-03-26")) {
        throw "capabilities missing 'mcp-2025-03-26' (v0.9.4 regression)"
    }
    "version=$($wk.version), gatewayMcpUrl=$($wk.gateway.mcpUrl)"
}

# 2. /api/discovery has beacon metadata that /.well-known omits
Check "/api/discovery includes generatedAtUtc and serverIpAddress" {
    $d = Invoke-RestMethod -Uri "$AdminUrl/api/discovery" -TimeoutSec $script:HttpTimeoutSec
    if (-not $d.generatedAtUtc)   { throw "missing generatedAtUtc" }
    if (-not $d.serverIpAddress)  { throw "missing serverIpAddress" }
    if (-not $d.instanceName)     { throw "missing instanceName" }
    "instanceName='$($d.instanceName)' serverIp=$($d.serverIpAddress)"
}

# 3. /api/beacon shape
Check "/api/beacon returns the broadcast payload shape" {
    $b = Invoke-RestMethod -Uri "$AdminUrl/api/beacon" -TimeoutSec $script:HttpTimeoutSec
    if (-not $b.ipAddress)        { throw "missing ipAddress" }
    if (-not $b.hostName)         { throw "missing hostName" }
    if (-not $b.instanceName)     { throw "missing instanceName" }
    "ipAddress=$($b.ipAddress) hostName=$($b.hostName)"
}

# 4. UDP beacon listen on $BeaconPort. The beacon broadcasts the
#    DiscoveryDocument JSON (per PHASE-03 design: provider-era
#    BeaconAdvertisement is exposed by /api/beacon for browser
#    backward-compat, but the wire UDP broadcast carries the canonical
#    gateway-first discovery doc). We check the DiscoveryDocument
#    fields, not the legacy BeaconAdvertisement fields.
Check "UDP beacon broadcasts DiscoveryDocument within $BeaconTimeoutSec seconds on port $BeaconPort" {
    # v0.9.6: keep listening until we receive a DiscoveryDocument-shaped
    # payload from the local instance. On a busy LAN there may be other
    # MCOS instances (or older builds emitting the legacy
    # BeaconAdvertisement shape) broadcasting on the same port; the
    # self-test should pass as long as the LOCAL instance's broadcast
    # arrives within the deadline. Pre-v0.9.6 the test took the first
    # packet off the wire and FAILed if it happened to be from a
    # different host running an older protocol version.
    $listener = $null
    try {
        $listener = New-Object System.Net.Sockets.UdpClient
        $listener.Client.SetSocketOption(
            [System.Net.Sockets.SocketOptionLevel]::Socket,
            [System.Net.Sockets.SocketOptionName]::ReuseAddress,
            $true)
        $listener.Client.Bind((New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, $BeaconPort)))
        $deadline = [DateTime]::UtcNow.AddSeconds($BeaconTimeoutSec)
        $sawForeign = 0
        while ([DateTime]::UtcNow -lt $deadline) {
            $remainingMs = [int][Math]::Max(100, $deadline.Subtract([DateTime]::UtcNow).TotalMilliseconds)
            $listener.Client.ReceiveTimeout = $remainingMs
            $endpoint = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)
            try {
                $bytes = $listener.Receive([ref]$endpoint)
            } catch [System.Net.Sockets.SocketException] {
                throw "no DiscoveryDocument-shaped beacon received within $BeaconTimeoutSec seconds (saw $sawForeign foreign-shaped packets from other hosts)"
            }
            $message = [System.Text.Encoding]::UTF8.GetString($bytes)
            try {
                $payload = $message | ConvertFrom-Json -ErrorAction Stop
            } catch {
                $sawForeign++
                continue
            }
            # DiscoveryDocument has `product` + `gateway.mcpUrl`.
            # Legacy BeaconAdvertisement has `browserPort` + `platformGateways`.
            # We only accept the former.
            if (-not $payload.product -or -not $payload.gateway -or -not $payload.gateway.mcpUrl) {
                $sawForeign++
                continue
            }
            if ($payload.auth -ne "none")  { throw "beacon JSON: auth != 'none'" }
            if ($payload.trust -ne "lan")  { throw "beacon JSON: trust != 'lan'" }
            return "received $($bytes.Length)-byte DiscoveryDocument from $($endpoint.Address) -- serverIp=$($payload.serverIpAddress) gateway=$($payload.gateway.mcpUrl) (skipped $sawForeign foreign packets)"
        }
        throw "no DiscoveryDocument-shaped beacon received within $BeaconTimeoutSec seconds"
    } finally {
        if ($listener) { $listener.Close() }
    }
}

# 5. DNS-SD: best-effort. Resolve-DnsName works against unicast DNS.
#    On a host with Bonjour / mDNSResponder, _mcos._tcp.local resolves
#    via the Win32 DNS-SD shim. On a host without Bonjour, this is
#    expected to FAIL -- treat as informational, not blocking.
Write-Host "[?] DNS-SD: _mcos._tcp.local PTR (informational; Bonjour required)" -NoNewline
try {
    $dns = Resolve-DnsName -Name "_mcos._tcp.local" -Type PTR -ErrorAction Stop -QuickTimeout 2>$null
    Write-Host "  PASS" -ForegroundColor Green
    "    found $($dns.Count) PTR record(s)"
    $script:pass++
} catch {
    Write-Host "  SKIP" -ForegroundColor Yellow
    "    Resolve-DnsName could not reach mDNS resolver (Bonjour/mDNSResponder not running, or unicast DNS doesn't proxy). This is not a failure."
}

# 6. Onboarding profile shape (touch every typed client)
$profileFails = 0
foreach ($client in @("generic","claude-code","codex","grok","chatgpt")) {
    Check "Onboarding profile '$client' is well-formed" {
        $p = Invoke-RestMethod -Uri "$AdminUrl/api/onboarding/$client" -TimeoutSec $script:HttpTimeoutSec
        if (-not $p.gatewayMcpUrl)        { throw "missing gatewayMcpUrl" }
        if ($p.authRequired -ne $false)   { throw "authRequired != false" }
        if ($p.trust -ne "lan")           { throw "trust != lan" }
        if ($p.configSnippets.Count -lt 1) { throw "no configSnippets" }
        "gatewayMcpUrl=$($p.gatewayMcpUrl)"
    }
}

Write-Host ""
Write-Host "=== Summary ===" -ForegroundColor Cyan
"  PASS: $pass"
"  FAIL: $fail"

if ($fail -eq 0) {
    Write-Host "MCOS discovery self-test: PASS" -ForegroundColor Green
    exit 0
} else {
    Write-Host "MCOS discovery self-test: FAIL ($fail check(s))" -ForegroundColor Red
    exit 1
}
