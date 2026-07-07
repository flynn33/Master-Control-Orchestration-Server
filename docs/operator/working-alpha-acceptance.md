# MCOS Working-Alpha Acceptance Runbook

> This is an internal alpha validation runbook for a trusted LAN. Do not expose
> MCOS to an untrusted network. Final build, packaging, installation, service
> mutation, firewall mutation, URL ACL mutation, TLS binding, and live LAN
> validation are operator-gated actions.

## 1. Purpose and environment assumptions

This runbook proves that a Master Control Orchestration Server (MCOS) install is
a **working alpha**: installed, running, discoverable on a trusted LAN, usable
by at least one registered LAN client, and diagnosable on failure. It validates
the *running product*, not just that files exist on disk.

Assumptions:

- One **MCOS host** (Windows) and one **second LAN host** on the same trusted
  subnet for the end-to-end client test.
- The operator has local administrator rights on the MCOS host.
- The network is a trusted LAN. MCOS AI-client surfaces use network-level trust,
  not application-layer authentication.

## 2. Prerequisites

**MCOS host — Windows 11 or Windows Server 2022:**

- Local administrator privileges (install, service, firewall, URL ACL, TLS).
- PowerShell 7+ (`pwsh`) recommended; Windows PowerShell 5.1 works for most
  gates. The acceptance scripts target `pwsh`.
- The Microsoft Visual C++ runtime and .NET dependencies bundled by the MSI.

**Second LAN host (Gate E):**

- Any OS with PowerShell 7+ and network line-of-sight to the MCOS host.
- A copy of `Test-MasterControlLanClientAcceptance.ps1`,
  `MasterControlAcceptanceCommon.ps1`, and the exported client bundle.

**Build validation only (operator-gated, optional):** Visual Studio 2022/2026
with the C++ workload, CMake, and vcpkg. The package's `windows-debug` preset
maps to this repository's **`debug`** CMake preset.

## 3. Required elevated privileges

Run installation, repair, uninstall, and any binding validation from an
**elevated** (Administrator) PowerShell. The acceptance/registration/diagnostics
scripts themselves are non-mutating and do not require elevation, though
diagnostics collects more (event log, service state) when elevated.

## 4. Required ports and network profiles

| Surface | Default port | Protocol | Firewall profiles |
|---|---|---|---|
| Admin / browser dashboard | 7300 | TCP | Private, Domain |
| MCP gateway | 8080 | TCP | Private, Domain |
| MCP gateway (TLS, optional) | 8443 | TCP | Private, Domain |
| Discovery beacon (optional) | configured | UDP | Private, Domain |
| DNS-SD / mDNS (optional) | 5353 | UDP | Private, Domain |

The Public profile is intentionally excluded. Trusted-LAN posture must never
advertise loopback (`127.0.0.1`, `localhost`, `::1`) or wildcard (`0.0.0.0`,
`::`) hosts to LAN clients.

## 5. Trust posture

Internal alpha, trusted LAN only. AI clients are external and connect to one
advertised MCP gateway endpoint. There is no app-layer client authentication;
trust is enforced at the network level.

## 6. Installer / bootstrapper command sequence (operator-gated, elevated)

Use the supported installer. Non-mutating inspection first:

```powershell
# Non-mutating environment + preflight inspection (safe).
.\dist\debug\MasterControlBootstrapper.exe detect --json
.\dist\debug\MasterControlBootstrapper.exe preflight "C:\Program Files\Master Control Orchestration Server" --json
```

Install (mutating — operator authorization required):

```powershell
# Preferred: the MSI / setup wizard.
.\MasterControlOrchestrationServerSetup.exe

# Or the bootstrapper install verb directly.
.\dist\debug\MasterControlBootstrapper.exe install "C:\Program Files\Master Control Orchestration Server" --json
```

Confirm the running install (reports service state, firewall/URL ACL bindings):

```powershell
.\dist\debug\MasterControlBootstrapper.exe validate "C:\Program Files\Master Control Orchestration Server" --json
```

## 7. Local working-alpha acceptance (Gate D)

Run on the MCOS host after install + service start:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass `
  -File scripts\Test-MasterControlOrchestrationServerWorkingAlpha.ps1 `
  -Mode LocalInstalledRuntime `
  -OutputDirectory .\artifacts\working-alpha-local `
  -Json
```

This probes every required admin endpoint (with schema checks), the MCP gateway
`initialize`/`ping`/`tools/list`, discovery routability for the current posture,
and — when `-BootstrapperPath`/`-InstallDirectory` are supplied — required
HTTP.sys bindings. It writes a JSON report, a Markdown summary, and per-probe
evidence, and exits nonzero on any required failure. It is non-mutating.

## 8. Register and export a LAN client bundle

On the MCOS host (loopback local-operator authority; read-only by default):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass `
  -File scripts\Register-MasterControlLanClient.ps1 `
  -ClientId codex-alpha-01 `
  -ClientType codex `
  -DisplayName "Codex Alpha Validation Client" `
  -OutputPath .\artifacts\clients\codex-alpha-01.json `
  -Json
```

No mutation privileges are granted unless explicit `-Privilege` names are passed
(e.g. `-Privilege canCreateMcpServers`). Use `-WhatIf` for a dry run. Copy the
exported bundle to the second host.

## 9. Second-host LAN client acceptance (Gate E)

From the **second LAN host** (not the MCOS host):

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass `
  -File .\Test-MasterControlLanClientAcceptance.ps1 `
  -BundlePath .\codex-alpha-01.json `
  -OutputDirectory .\mcos-lan-acceptance `
  -Json
```

This fetches discovery, **rejects loopback/wildcard advertised URLs**, probes
gateway status/health, posts a telemetry heartbeat and an authenticated client
heartbeat (`X-MCOS-Client-Id`), completes MCP `initialize`/`ping`/`tools/list`,
and confirms the client appears in `/api/telemetry/clients` with a fresh
liveness timestamp. Exits nonzero on any required failure.

## 10. Diagnostics collection (Gate F)

Run after any failure and after a passing acceptance:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass `
  -File scripts\Get-MasterControlOrchestrationServerDeploymentDiagnostics.ps1 `
  -OutputRoot .\artifacts\diagnostics `
  -Bundle
```

Collects install state, installer/runtime/telemetry logs, service status, service
event-log records, `netsh http show urlacl`, `netsh http show sslcert`, firewall
rules, port listeners, and live admin/gateway probe outputs into a JSON summary,
a Markdown summary, and a `.zip` bundle. Collection is read-only and exits
nonzero only if diagnostics collection itself fails — not because the product
under test failed.

## 11. Safe repair (operator-gated, elevated)

```powershell
.\dist\debug\MasterControlBootstrapper.exe repair "C:\Program Files\Master Control Orchestration Server" --json
```

## 12. Safe uninstall (operator-gated, elevated)

```powershell
.\dist\debug\MasterControlBootstrapper.exe uninstall "C:\Program Files\Master Control Orchestration Server" --json
```

Uninstall removes the service, firewall rules, URL ACL reservation, shortcuts,
and uninstall registration. Add `--purge-data` only when you intend to delete
the data directory.

## 13. Pass / fail criteria

**Gate D — local installed runtime (all must hold):**

- MSI/bootstrapper install succeeded; service registered, running, auto-start,
  delayed auto-start, recovery configured, SID unrestricted.
- Install state exists and points to real files/directories.
- Admin listener answers all required probes with valid schemas.
- Gateway answers `initialize`/`ping`/`tools/list`.
- Discovery advertises routable URLs in LAN mode (no loopback/wildcard).
- Required URL ACL / firewall / TLS bindings are present (or correctly reported
  as optional when the mode does not require them).
- Diagnostics bundle created. Script exits 0.

**Gate E — second-host LAN (all must hold):**

- The second host fetches `/.well-known/mcos.json`; advertised URLs are
  reachable and are not loopback/wildcard placeholders.
- `/api/discovery` and gateway status/health probes succeed.
- Telemetry heartbeat and authenticated `/api/client/heartbeat` succeed.
- MCP `initialize`, `ping`, and `tools/list` succeed (`tools/list` returns a
  `tools` array).
- The client appears in telemetry/liveness with a fresh timestamp. Evidence
  bundle created. Script exits 0.

## 14. Evidence to preserve

- `artifacts\working-alpha-local\` — Gate D JSON/Markdown report + `evidence\`.
- `artifacts\clients\<clientId>.json` — the exported client bundle.
- `mcos-lan-acceptance\` — Gate E report + `evidence\` from the second host.
- `artifacts\diagnostics\...\*.zip` — the diagnostics bundle.
- The bootstrapper `validate --json` output (binding posture).

## 15. Operator-gated actions the agent must not run without authorization

- Final Windows release build, packaging, and deployment.
- MSI installation / service registration or mutation.
- Firewall mutation, URL ACL mutation, TLS certificate binding.
- Live LAN validation on real hosts (Gate D managed install, Gate E second host).
- Any push, pull request, tag, or release publication.

The acceptance, registration, and diagnostics scripts in this runbook are
non-mutating and safe to run repeatedly against an already-installed instance.
