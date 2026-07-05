# Deployment Acceptance

This page defines what it means for a Master Control Orchestration Server (MCOS)
build to be **deployable and usable** on a Windows LAN, and how to prove it. It
separates what the repository CI gate proves from what only a target-host
operator can prove, and gives non-destructive checks that run by default plus
operator-authorized checks that mutate the host.

MCOS is internal alpha software. No status on this page should be claimed without
the evidence the corresponding section requires.

## 1. Acceptance levels

Use these terms precisely. Each level assumes every level above it.

| Level | Meaning | Proven by |
|---|---|---|
| **Buildable** | CMake configure/build and unit tests pass on the intended Windows toolchain. | Product gate, or a local `ctest` run. |
| **Packageable** | The release package script produces the MSI/zip/metadata and the staged bootstrapper preflight passes. | Product gate package artifact, or a local `Package-*` run. |
| **Installed** | The MSI (or bootstrapper) completed a successful install on a target Windows host. | Operator-authorized install (Gate D). |
| **Service-running** | The `MasterControlProgram` service exists, starts, and answers local health probes. | Non-destructive local probe (Gate A2) on the installed host. |
| **LAN-discoverable** | DNS-SD/mDNS or the UDP beacon is observed from a LAN peer under a Private/Domain profile. | Operator-authorized second-host test (Gate E). |
| **Deployment-qualified** | Installed, service-running, locally healthy, LAN-discoverable, and verified with at least one real LAN client. | Gates D + E with captured evidence. |

Do **not** describe CI-only package artifacts as *installed*, *service-running*,
*LAN-discoverable*, or *deployment-qualified*.

## 2. What the product gate proves

The `windows-build-test-package.yml` workflow is the repository health gate for
each pushed commit and pull request (see [Release Gate](Release-Gate)). For a
given commit it proves:

- The tree **builds** on the CI Windows toolchain.
- Unit tests **pass**.
- The release **package** (MSI + zip + `PACKAGE-METADATA.json`) is produced.
- The **staged bootstrapper preflight** succeeds against the packaged payload
  (run with `--skip-service --skip-firewall --skip-uninstall-registration`, so it
  validates layout/readiness only).

## 3. What the product gate does NOT prove

The gate stops at the packaged artifact. It does **not** prove that:

- The MSI installs on a target host.
- The `MasterControlProgram` Windows service registers, starts, and stays healthy.
- `http://localhost:7300/api/health`, `/api/discovery`, and `/api/gateway/status`
  answer with valid JSON on the installed host.
- Windows Firewall rules and the HTTP.sys URL ACL match the selected install
  options.
- DNS-SD/mDNS or the UDP beacon is visible from a second LAN host.
- A real LAN client can reach the MCP gateway and register in `/api/clients`.

These require the operator-host gates below.

## 4. Non-destructive local runtime validation (Gate A2 — no authorization needed)

Run these on the **installed** host. They only observe existing state — no
install, service, firewall, URL ACL, TLS, or uninstall changes.

```powershell
# Deployed-runtime probe: install dir, config, service, /api/health, /api/discovery,
# /api/gateway/status, /api/clients, firewall-rule presence, URL ACL presence.
powershell -NoProfile -ExecutionPolicy Bypass -File `
  scripts\Test-MasterControlOrchestrationServerDeployedRuntime.ps1

# Discovery / DNS-SD / UDP-beacon self-test (local surface).
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-mcos-discovery.ps1
```

The deployed-runtime probe writes a JSON report and a Markdown summary (see
§8). A `service-running-local-healthy` assessment means the local surface is
sound; it does **not** by itself make the host *deployment-qualified* (that also
needs Gates D + E).

Default ports and identifiers the probes check (from
`src/MasterControlApp/MasterControlDefaults.cpp` and
`src/MasterControlBootstrapper/main.cpp`):

| Item | Value |
|---|---|
| Admin / dashboard / API | TCP `7300` (`browserPort`) |
| MCP gateway | TCP `8080` (`mcpGateway.listenPort`) |
| Discovery beacon | UDP `7301` (`beaconPort`) |
| DNS-SD / mDNS | UDP `5353` |
| Windows service name | `MasterControlProgram` |
| Firewall rule prefix (installer-created) | `Master Control Orchestration Server - …` |
| Config file | `%ProgramData%\MasterControlOrchestrationServer\config\master-control-orchestration-server.json` |

## 5. Operator-authorized managed install validation (Gate D — mutates the host)

Run only when the operator explicitly authorizes install testing on an elevated
Windows host. These **mutate** the host (install, service, firewall rules, URL
ACL). See [Maintenance](Maintenance) and
[Packaging and Gateway Binary](Packaging-and-Gateway-Binary).

```powershell
$Msi = "dist\packages\release\MasterControlOrchestrationServer-vA3.11.0-win-x64\MasterControlOrchestrationServer-vA3.11.0-win-x64.msi"
msiexec /i $Msi /l*v "$env:TEMP\mcos-install.log"

& "C:\Program Files\Master Control Orchestration Server\MasterControlBootstrapper.exe" preflight --json
Get-Service MasterControlProgram
Invoke-RestMethod http://localhost:7300/api/health          | ConvertTo-Json
Invoke-RestMethod http://localhost:7300/api/discovery       | ConvertTo-Json -Depth 6
Invoke-RestMethod http://localhost:7300/api/gateway/status  | ConvertTo-Json -Depth 4

# Firewall + URL ACL should match the install options (checkbox on = rules created):
Get-NetFirewallRule -DisplayName 'Master Control Orchestration Server - *' |
  Format-Table DisplayName, Enabled, Direction, Action, Profile -AutoSize
netsh http show urlacl | Select-String '8080'
```

Firewall and service integration are **installer-managed by default**: the MSI's
*Configure Windows Firewall rules* and *Install service* checkboxes drive the
bootstrapper, which creates the rules and registers the service unless the
checkboxes are unticked (`--skip-firewall` / `--skip-service`). See
[Windows Firewall and LAN Mode](Windows-Firewall-LAN-Mode).

## 6. Second-host LAN validation (Gate E — requires a second LAN host)

Run only when the operator provides a second LAN host on a Private/Domain profile
and authorizes the test.

```powershell
# From the LAN peer (replace <mcos-host>):
Invoke-RestMethod http://<mcos-host>:7300/api/discovery         | ConvertTo-Json -Depth 6
Invoke-RestMethod http://<mcos-host>:7300/api/gateway/status    | ConvertTo-Json -Depth 4
Invoke-RestMethod http://<mcos-host>:7300/api/onboarding/generic | ConvertTo-Json -Depth 8
# DNS-SD: dns-sd -B _mcos._tcp (macOS), avahi-browse _mcos._tcp (Linux),
#         or Resolve-DnsName _mcos._tcp.local -Type PTR -LlmnrFallback (Windows).
```

Confirms LAN reachability and that DNS-SD/beacon advertising crosses the host
firewall. See [LAN Discovery](LAN-Discovery).

## 7. Live LAN-client validation (deployment-qualified evidence)

Drive a real MCP client (Claude Code, Codex, Grok, ChatGPT, or generic MCP)
through `http://<mcos-host>:8080/mcp` from a LAN peer, then confirm on the MCOS
host:

```powershell
Invoke-RestMethod http://localhost:7300/api/clients | ConvertTo-Json -Depth 6
```

The client must appear with a fresh `lastSeenUtc` / presence timestamp. This is
the only evidence that makes the host **deployment-qualified**. Do not fabricate
or infer it — it requires a real client round trip.

## 8. Evidence collection and report paths

Store acceptance evidence under the local (gitignored) artifacts tree:

```text
artifacts/deployability-audit/
  AUDIT.md                                  # loop ledger
  audit.json                                # machine-readable ledger
  deployed-runtime/deployed-runtime-report.json   # Gate A2 JSON
  deployed-runtime/deployed-runtime-summary.md     # Gate A2 Markdown
  preflight.json                            # Gate D bootstrapper preflight
  local-health.json / local-discovery.json / gateway-status.json
  firewall-rules.txt / urlacl.txt
  lan-peer-discovery.json                   # Gate E (when authorized)
  lan-client-evidence.json                  # §7 (when authorized)
```

`scripts\Test-MasterControlOrchestrationServerDeployedRuntime.ps1` writes the
`deployed-runtime/*` reports by default; pass `-ReportPath` / `-SummaryPath` to
redirect. Pass `-EmitOperatorGateCommands` to print (never run) the Gate D/E
commands.

## 9. Pass/fail matrix

| Gate | Scope | Authorization | Pass criteria |
|---|---|---|---|
| A1 | Static (`Invoke-MCOSRemediationGates -SkipBuild`, JSON/link/metadata) | None | All static gates green. |
| A2 | Non-destructive deployed-runtime probe | None | `assessment = service-running-local-healthy`; required probes PASS. |
| B | Build + unit tests (`ctest`) | Operator (Windows toolchain) | Build succeeds; all tests pass. |
| C | Release packaging (MSI/zip/metadata + staged preflight) | Operator (+ WiX 5) | MSI present and non-trivial; preflight succeeds. |
| D | Managed MSI install / service / firewall / URL ACL | Operator (elevated host) | Install succeeds; service healthy; endpoints valid; rules/ACL match options. |
| E | Second-host LAN discovery + live client | Operator (second host + client) | Peer observes discovery; a real client refreshes `/api/clients`. |

**Deployment-qualified** requires A1, A2, B, C, D, and E all passing with captured
evidence. If the operator does not authorize D or E, the honest status is
**deployment-ready pending operator acceptance**, not deployment-qualified.

## 10. Known alpha limitations

- Alpha versions are published as GitHub pre-releases with the MSI installer;
  deployment can also use CI package artifacts or local packaging (see
  [Release Gate](Release-Gate)).
- Local MSI packaging (Gate C) needs **WiX 5**. The WinUI Shell builds with the
  Visual Studio 2026 **v145** toolset by default (the CI toolset); a Visual
  Studio 2022 host can build the Shell locally by configuring with
  `-DMASTERCONTROL_SHELL_PLATFORM_TOOLSET=v143`.
- `auth=none, trust=lan`: there is no app-layer authentication on the gateway;
  the network firewall (Private/Domain profiles only) is the load-bearing control.
- DNS-SD live presence and end-to-end client integration are operator-driven and
  require a second host; they are not proven by the product gate.

## See also

- [Quick Start](Quick-Start) — build/install and first local checks
- [Windows Firewall and LAN Mode](Windows-Firewall-LAN-Mode) — firewall ownership and rules
- [LAN Discovery](LAN-Discovery) — DNS-SD, beacon, discovery documents
- [Maintenance](Maintenance) — restore, upgrade, repair, uninstall
- [Release Gate](Release-Gate) — alpha release policy and the product gate
