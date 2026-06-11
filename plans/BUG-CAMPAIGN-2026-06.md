# MCOS Bug-Fix + Deferred-Feature Campaign — Findings Report

**Branch:** `fix/bug-campaign-2026-06` · **Date:** 2026-06-11 · **Baseline:** `5d56065` (main)

A maintainer asked for the LAN MCP gateway to be made to "work properly" — find and
fix bugs, implement the features that don't work — without a Windows build/test loop
available (work performed on macOS). Every C++ change is therefore verified by static
reasoning + the regression tests below; the **Windows validation checklist** at the end
is the acceptance gate the maintainer must run.

13 commits. Calibration note: the codebase is meticulous and heavily self-documented;
of ~9 candidate "bugs" surfaced by exploration, all but the ones below were false
positives — recorded in the **False alarms** section so they are not re-litigated.

---

## 1. Fixed bugs

| # | Symptom (user-visible) | Root cause | Fix | Commit |
|---|---|---|---|---|
| 1 | A LAN client reading the advertised gateway port from `/api/beacon` connects to the **admin listener (7300)** instead of the MCP gateway (8080). | `BeaconService::currentAdvertisement()` passed `configuration.browserPort` into BOTH port slots of the `BeaconAdvertisement` aggregate (`browserPort`, `gatewayPort`). | Second slot → `configuration.mcpGateway.listenPort`. Regression test `testBeaconAdvertisementJsonShape`. | `ae87987` |
| 2 | Every client config produced by the **Exports** surface (`.claude.json`, `Install-ClaudeGateway.ps1`, `codex-mcp.json`, openai/xai profiles) points at `http://<host>:7200/mcp/gateway` → connection refused. | `gatewayUrl` was composed from the seeded `platform-gateway` inventory row (port **7200** — the external gateway retired at v0.9.0) and the `/mcp/gateway` admin-document path, never realigned to ADR-002/003. | Compose from `cfg.mcpGateway.listenPort + mcpPath` (the v0.10.8 supervisor-config fix, applied here too). Seed row 7200 → 8080. Test `testSeededGatewayEndpointPointsAtNativeGateway`. | `86ad1a3` |
| 3 | UDP beacon silently stops reaching the LAN (firewall rule, adapter loss) with **zero operator signal** — the beacon "appears to run." | `socket()` failure exited the worker silently; every `sendto()` return value was discarded. | Edge-triggered diagnostics events: `beacon_socket_create_failed` (error), `beacon_broadcast_failed`/`_recovered` (warning/info, one per failure window). | `0ffd627` |
| 4 | ~25 events in `/api/activity` show `"method":""` (flagged by the 2026-04-19 operator probe), breaking verb-based log filtering. | The four supervisor lifecycle ring events (`supervisor_select/_config_issue/_config_regenerate/_revoke`) constructed `ActivityEvent` without `evt.method`, though `request.method` was in scope — every sibling request-derived event stamps it. | Stamp `evt.method = request.method` at all four sites. | `08c0c87` |

**Severity:** #1 and #2 are the load-bearing "the software doesn't work" bugs — both break the
core promise (a LAN client following MCOS's own advertised/exported config cannot reach the
gateway). #3 and #4 are honesty/observability fixes.

---

## 2. Features implemented (deferred items closed)

Maintainer chose full scope ("Everything"). Each is its own commit with tests where a seam exists.

| Feature | What landed | Commit |
|---|---|---|
| Governance-bundle platform-awareness | `/api/onboarding/{client}?platform=windows\|macos\|ios` (alias `?os=`); was hardcoded `windows`. Fallback unchanged. | `fbcdb5a` |
| Log rotation age/count bounds | 14-day + 200k-row bounds beside the v0.10.21 50 MB bound; deep check throttled 10 min/path. Test `testDiagnosticsCapturedAtUtcParse`. | `be964f2` |
| Cert auto-rotation | `scripts/Register-CertAutoRotation.ps1` — weekly scheduled task: reuse-or-renew + thumbprint sync via `POST /api/config`. In MSI payload. | `e947ace` |
| Beacon payload signing | Additive `signature` (HMAC-SHA256) gated on `security.beaconSigningEnabled` + `beaconSigningKey` (auto-generated fresh; legacy = unsigned). Back-compat test in `testDefaultConfiguration`. | `92edd8f` |
| **PHASE-14 Slice E** | `SqliteDiagnosticsStore` (WAL + schema migration) + `DiagnosticsService` (ring + store, self-test persistence, audited clear) + **functional `POST /api/diagnostics/clear`** (was 501). 4 store/service tests. | `4788192` |
| **PHASE-14 Slices B + D** | 6 `mcos_diagnostics_*` MCP tools; browser dashboard Diagnostics tab. | `9404fcd` |
| **PHASE-14 Slice C** | WinUI Shell `DiagnosticsSectionControl` + ShellRuntime fetch helpers + nav wiring. | `08532a9` |
| Admin listener TLS | Opt-in SChannel TLS on 7300 (`security.adminTlsEnabled` + `adminTlsCertThumbprint`); bad cert → falls back to plain HTTP. Test `testSecuritySettingsAdminTlsDefaults`. | `6f05161` |
| Tile-grid expand-on-click | Tap a tile → host:port + full client roster; hover tooltip. | `3b75c66` |

**Closed as already-done (stale deferred ledger entries):** CLU `enforceAction` wiring (the
route-layer `enforceGovernance` helper already routes every mutation handler through
`enforceAction`, Block→403, RequiresOperatorApproval→202 staged action); legacy `GatewayType`
tombstone (enum already only `Native`; bootstrapper reference removed at v0.10.14).

**Genuinely deferred (cannot be done off-Windows):** per-pass self-test log rows (opt-in flag);
PHASE-13 Win2D/Direct2D shell visuals (needs an interactive Windows render/build loop).

---

## 3. False alarms (disproved — do not re-open)

- **`SupervisorAssignmentService.cpp:484` heartbeat watchdog "inverted condition".** `if (now - last <= threshold) return false;` is correct: it returns "no timeout" while the heartbeat is recent and falls through to mark Disconnected once stale. Not inverted.
- **`SupervisorAssignmentService.cpp:529-558` Disabled-mode "capability leak".** `capabilitiesForMode` returns empty for Disabled; the `defaultAutonomousSupervisorCapabilities()` branch is only reached for `AutonomousSupervisor`. The Disabled→Autonomous default at :258 is an intentional default-on-absent-field, documented.
- **`McpGatewayAdapters.cpp:~894` `highRisk = !requiredCapabilities.empty()`.** Repo-wide convention (identical at `MasterControlDefaults.cpp:511`; consumers test `highRisk || !requiredCapabilities.empty()`). Not a bug.
- **`BCryptGenRandom == 0` "fragile NTSTATUS check".** `STATUS_SUCCESS` is 0 by definition; the comparison is correct and has a fallback. No change warranted.
- **mDNS `registration_failed` (audit doc, 2026-04-19).** The `PlatformServiceCatalogService::registerGatewayLocked` path already carries the v0.9.36 remediation (non-null completion callback, `DNS_REQUEST_PENDING` accepted, `last_register_error` diagnostics). The audit predates the fix. See runbook below — field-diagnose, do not "fix" blind.

---

## 4. Needs-Windows investigation — mDNS runbook

If `/api/platform-services/gateways` still reports `registration_failed` after this build:

1. `curl http://127.0.0.1:7300/api/platform-services/gateways` → read each lane's
   `properties.last_register_error` / `last_register_error_decimal`.
2. `0x00000057` (87, ERROR_INVALID_PARAMETER) would indicate the pre-v0.9.36 null-callback
   bug — should be impossible on this tree; if seen, the build is stale.
3. Other codes → environment: the **Dnscache** service / Bonjour mDNS responder must be
   running, the process needs the privilege to register (LocalSystem when installed as the
   service has it; console-mode as a normal user may not), and the Windows Firewall mDNS
   rule (UDP 5353) must be open — the bootstrapper installs it; verify with
   `Get-NetFirewallRule -DisplayName 'Master Control* mDNS*'`.
4. Confirm from a second LAN host: `dns-sd -B _mcos-mcp._tcp` (macOS/Bonjour) should list the
   instance.

The shipping discovery path (`DiscoveryService`, the `_mcos._tcp` / `_mcos-mcp._tcp` /
`_mcos-onboarding._tcp` records) is independent of the platform-gateway lanes and was reviewed
clean.

---

## 5. Windows validation checklist (maintainer runs this)

All C++/tests are **written but not yet compiled or run** — no MSVC/WinUI/HTTP.sys toolchain
on the authoring host. JS (`node --check`) and Python (`ast.parse`) edits were validated locally.

1. **Build + unit tests.**
   `scripts/Build-MasterControlOrchestrationServer.ps1` (first build re-resolves vcpkg — note
   `sqlite3` is newly added to `vcpkg.json`), then `ctest` for the
   `MasterControlOrchestrationServerTests` target. New tests:
   `testBeaconAdvertisementJsonShape`, `testSeededGatewayEndpointPointsAtNativeGateway`,
   `testDiagnosticsCapturedAtUtcParse`, `testSqliteDiagnosticsStoreCrudAndFilters`,
   `testSqliteDiagnosticsStoreSelfTestPrune`, `testDiagnosticsServiceRingFallbackAndClear`,
   `testDiagnosticsServiceStoreBackedReportAndAuditedClear`,
   `testSecuritySettingsAdminTlsDefaults`, plus the beacon-signing assertions in
   `testDefaultConfiguration`.
2. **Gates.** `scripts/Test-MCOSStaticGates.ps1`, `Test-MCOSSecurityDefaults.ps1`,
   `Invoke-MCOSRemediationGates.ps1`, `check-mastercontrol-forsetti.ps1`.
3. **Deploy local.** `scripts/Deploy-LocalLive.ps1`, then
   `Test-MasterControlOrchestrationServerDeployment.ps1`.
4. **Beacon-fix acceptance (bug #1).** `scripts/check-mcos-discovery.ps1`, plus
   `curl http://127.0.0.1:7300/api/beacon` → assert `gatewayPort` == 8080 (== `mcpGateway.listenPort`)
   and **≠ 7300**.
5. **Exports-fix acceptance (bug #2).** `curl http://127.0.0.1:7300/api/exports` → the
   `claude` / `codex` / `gateway-profile` artifacts must contain `:8080/mcp` (the gateway URL),
   **never** `:7200/mcp/gateway`.
6. **Onboarding platform-awareness.** `curl '…/api/onboarding/claude-code?platform=macos'` →
   `governanceBundleUrl` ends `/macos`; no `?platform` → `/windows`.
7. **PHASE-14 diagnostics.** `curl …/api/diagnostics/summary` → `storeBacked:true` once events
   accumulate; `POST …/api/diagnostics/clear` `{"reason":"smoke"}` → `{ok:true, deletedRows:N}`
   (no longer 501). In the dashboard, open the **Diagnostics** tab → filter, Export, Clear. In
   the shell, open **Advanced → Diagnostics** → Export Markdown opens the save dialog. From
   Claude Code, `mcos_diagnostics_summary` returns the same payload.
8. **Admin TLS (opt-in).** Set `security.adminTlsEnabled:true` + `adminTlsCertThumbprint` to the
   cert from `Configure-LocalServerCert.ps1`; restart; `curl -k https://127.0.0.1:7300/api/health`.
   Then set a **bad** thumbprint and confirm the service still serves plain HTTP and logs
   `admin_tls_credentials_failed` (the no-brick guarantee).
9. **mDNS / DNS-SD.** Runbook §4; `dns-sd -B _mcos-mcp._tcp` from a second host.
10. **Shell UI.** `scripts/Test-ShellAllPages.ps1`, `Invoke-ShellUiProbe.ps1`; verify a tile
    expands on click.
11. **End-to-end.** Drive a real MCP client (`http://<host>:8080/mcp`) through `tools/list` +
    `tools/call`; confirm the client appears in `/api/clients` with a fresh `lastSeenUtc`
    (`Invoke-MasterControlOrchestrationServerIdeAcceptance.ps1`).

### Build-surface notes for the first Windows compile
- `vcpkg.json` adds `sqlite3`; CMake adds `find_package(unofficial-sqlite3 CONFIG REQUIRED)`
  and links `unofficial::sqlite3::sqlite3` (PUBLIC, so the test target resolves it).
- `secur32` added to `MasterControlApp` link libs for the SChannel TLS layer.
- New TUs: `DiagnosticsService.cpp`, `DiagnosticsStore.cpp`.
- New Shell files registered in `MasterControlShell.vcxproj` + `Project.idl`; the WinUI XAML
  compile is the most likely place to surface a fixup (no WinUI toolchain here) — the section
  was copied structurally from `TelemetrySectionControl` and the export dialog from
  `OverviewSectionControl`'s `IFileSaveDialog` pattern.
