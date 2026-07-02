# 12 - mDNS Gateway Registration Proof

Build: 48  |  Endpoint: http://127.0.0.1:7300/api/platform-services/gateways  |  Date: 2026-04-19

## Verdict: PRODUCT BUG (IPv6-only host path)

The three platform gateways (windows, macos, ios) all report
`status: "registration_failed"` because the Windows mDNS registration
code hard-codes the IPv6 pointer argument to `nullptr` and only parses
the descriptor IP with `InetPtonW(AF_INET, ...)`. On this host,
`SharedTelemetry::readPrimaryNetworkIdentity` returned an IPv6 ULA
(`fde3:c02c:3afa:4572:6687:be77:e9f1:32c8`), which fails AF_INET
parsing, so `DnsServiceConstructInstance` is called with no IP at all
and returns `NULL`, which deterministically sets the failure status.

This is NOT a host-configuration gap. Bonjour is not required
(the code uses Windows-native `dnsapi.dll`), Dnscache is running,
and UDP:5353 is bound by svchost (PID 3012) as expected on
Windows 11 26200.

## File:line to fix (do NOT apply here - locate only)

`src/MasterControlApp/MasterControlRuntime.cpp`

| Line | Current | Needed |
|------|---------|--------|
| 5762 | `PIP4_ADDRESS ipv4Pointer = nullptr;` | add sibling `PIP6_ADDRESS ipv6Pointer = nullptr;` |
| 5764 | `if (InetPtonW(AF_INET, ... ) == 1)` | also try `AF_INET6` into `IN6_ADDR` and populate an `IP6_ADDRESS` |
| 5773 | `nullptr,  // IPv6 pointer` | pass `ipv6Pointer` |
| 5708-5713 | `normalizeGatewayDescriptor` falls back to `primaryIpAddress` verbatim | prefer an IPv4 from the telemetry snapshot when the AF_INET6 path is not enabled, else fall back to `127.0.0.1` for loopback advertisement |

A secondary improvement: expose a `POST /api/platform-services/gateways/{moduleId}/retry`
(or similar) handler so operators can force re-registration without restarting
the runtime. The current HTTP surface at `MasterControlRuntime.cpp:10511`
is read-only.

## Host capability snapshot (informational)

- OS: Windows 11 Pro 10.0.26200
- Bonjour Service: NOT INSTALLED (registry absent) - not required
- Dnscache: Running
- UDP:5353: owned by svchost PID 3012 (native Windows mDNS responder)
- FDResPub: Running (unrelated to mDNS; WSD/SSDP)
- fdPHost: Stopped (unrelated)

## Evidence files (all under G:/Claude)

- `mcos_proof_mdns_code.txt` - full code + failure-path trace
- `mcos_proof_mdns_host_capability.txt` - Windows mDNS stack state
- `mcos_proof_mdns_gateway_state.json` - live /api/platform-services/gateways JSON
- `mcos_proof_mdns_retry.json` - retry attempt record (no endpoint exists)

## Reproduction

1. `Invoke-WebRequest http://127.0.0.1:7300/api/platform-services/gateways`
2. Observe all three entries have `ipAddress` = IPv6 ULA and
   `status` = `registration_failed`.
3. Inspect `src/MasterControlApp/MasterControlRuntime.cpp` lines
   5733-5799 (`registerGatewayLocked`).
4. Walk the failure path: `InetPtonW(AF_INET, "fde3:...")` returns 0
   -> `ipv4Pointer == nullptr` -> `DnsServiceConstructInstance(..., NULL, NULL, ...)`
   returns `NULL` -> status becomes `"registration_failed"` at line 5782;
   `DnsServiceRegister` is never called.

## Workaround without code change

Set `configuration.bindAddress` to an IPv4 literal (or disable IPv6 on the
primary adapter) and restart the runtime so that
`normalizeGatewayDescriptor` (line 5709) picks an IPv4 string that
`InetPtonW(AF_INET)` can parse.
