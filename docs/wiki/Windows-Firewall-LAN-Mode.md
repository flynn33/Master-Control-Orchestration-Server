# Windows Firewall and LAN Mode

PHASE-10 baseline (ADR-002 §1, §10).

## Trust model boundary

MCOS exposes two logically distinct surfaces. Both run on the host but are scoped differently:

| Surface | Audience | Auth | Trust | Where |
|---|---|---|---|---|
| AI-client MCP Gateway | AI clients on the trusted LAN (Claude Code, Codex, Grok, ChatGPT, generic MCP) | `auth=none` | `trust=lan` | Gateway port (default `8080`, configurable in `mcos.json`) at `/mcp` |
| Operator surface | The host operator(s) | LAN-trusted (no app-layer auth per ADR-001 §3) | `trust=lan` | Admin port — the dashboard at `/`, JSON API at `/api/*` |

**The trust boundary is enforced at the network layer, not the application layer.** Windows Firewall is the load-bearing control. Both surfaces refuse to participate when reachable from a Public network profile.

## Default ports

These are the values `buildDefaultConfiguration()` writes into `mcos.json` on first run. Adjust the firewall snippets below if your `mcos.json` has been edited:

| Surface | Default port | Field in `mcos.json` |
|---|---|---|
| Operator dashboard / admin API | TCP **7300** | `browserPort` |
| Discovery beacon (UDP JSON broadcast) | UDP **7301** | `beaconPort` |
| MCP Gateway (AI-client surface) | TCP **8080** | `mcpGateway.listenPort` |
| DNS-SD / mDNS | UDP **5353** (Win32 fixed) | n/a |

## One-shot apply (self-elevating PowerShell)

Most operators want all four rules with one paste. The block below works from any non-elevated PowerShell — it triggers a UAC prompt, applies the rules from the elevated child shell, and exits cleanly. The dashboard's **Settings → LAN Advertising and Windows Firewall** card emits the same snippets templated to the live ports of the running instance.

```powershell
$prog = 'C:\Program Files\Master Control Orchestration Server\MasterControlServiceHost.exe'
$rules = @(
  @{ Name = 'MCOS - Operator Surface (LAN)';  Protocol = 'TCP'; Port = 7300 },
  @{ Name = 'MCOS - MCP Gateway (LAN)';       Protocol = 'TCP'; Port = 8080 },
  @{ Name = 'MCOS - Discovery Beacon (LAN)';  Protocol = 'UDP'; Port = 7301 },
  @{ Name = 'MCOS - DNS-SD / mDNS (LAN)';     Protocol = 'UDP'; Port = 5353 }
)
$inner = $rules | ForEach-Object { @"
New-NetFirewallRule -DisplayName '$($_.Name)' -Direction Inbound -Action Allow ``
  -Protocol $($_.Protocol) -LocalPort $($_.Port) -Profile Private,Domain ``
  -Program '$prog' | Out-Null
Write-Host 'Created: $($_.Name) ($($_.Protocol) $($_.Port))'
"@ }
$tmp = [IO.Path]::ChangeExtension([IO.Path]::GetTempFileName(), '.ps1')
$inner -join "`n" | Set-Content -LiteralPath $tmp -Encoding UTF8
Start-Process powershell -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File',$tmp -Verb RunAs -Wait
Remove-Item -LiteralPath $tmp -Force
Get-NetFirewallRule -DisplayName 'MCOS *' | Format-Table DisplayName, Enabled, Direction, Action -AutoSize
```

If you'd rather paste rules one at a time from an already-elevated PowerShell, the four explicit snippets are below.

## Required Windows Firewall rules

After installation, add the following inbound rules. The `New-NetFirewallRule` snippets below are run as administrator.

### 1. AI-client MCP Gateway (LAN-only)

```powershell
New-NetFirewallRule `
  -DisplayName "MCOS — MCP Gateway (LAN)" `
  -Direction Inbound `
  -Action Allow `
  -Protocol TCP `
  -LocalPort 8080 `
  -Profile Private,Domain `
  -Program "C:\Program Files\Master Control Orchestration Server\MasterControlServiceHost.exe"
```

Adjust `LocalPort` to match `mcos.json`'s `mcpGateway.listenPort`. The `Profile Private,Domain` value is the binding constraint: do not include `Public`. If your LAN is on the Public profile (rare, but possible on misconfigured hosts), change the profile classification on the connection rather than weakening this rule.

### 2. Operator dashboard / admin API (LAN-only)

```powershell
New-NetFirewallRule `
  -DisplayName "MCOS — Operator Surface (LAN)" `
  -Direction Inbound `
  -Action Allow `
  -Protocol TCP `
  -LocalPort 7300 `
  -Profile Private,Domain `
  -Program "C:\Program Files\Master Control Orchestration Server\MasterControlServiceHost.exe"
```

Adjust `LocalPort` to match `mcos.json`'s `browserPort` (default `7300`). Same `Profile` constraint.

### 3. DNS-SD / mDNS LAN advertising (PHASE-03)

```powershell
New-NetFirewallRule `
  -DisplayName "MCOS — DNS-SD/mDNS (LAN)" `
  -Direction Inbound `
  -Action Allow `
  -Protocol UDP `
  -LocalPort 5353 `
  -Profile Private,Domain `
  -Program "C:\Program Files\Master Control Orchestration Server\MasterControlServiceHost.exe"
```

The Win32 DNS-SD APIs (`DnsServiceRegister`, `DnsServiceBrowse`) bind to UDP 5353. Without this rule, MCOS advertises but no client receives. Some Bonjour/mDNS responder installations create their own rule; if so, this rule is redundant.

### 4. UDP beacon (PHASE-03 backward compatibility)

```powershell
New-NetFirewallRule `
  -DisplayName "MCOS — Discovery Beacon (LAN)" `
  -Direction Inbound `
  -Action Allow `
  -Protocol UDP `
  -LocalPort 7301 `
  -Profile Private,Domain `
  -Program "C:\Program Files\Master Control Orchestration Server\MasterControlServiceHost.exe"
```

The beacon broadcasts on the configurable port `mcos.json::beaconPort` (default `7301`). Newer clients use DNS-SD; this rule keeps the legacy UDP-JSON browser tooling working.

## Public profile blocks

If MCOS is reachable on a Public profile, treat that as a misconfiguration and either:
- Reclassify the connection (Settings → Network → set the network to Private), or
- Bind MCOS to a specific LAN-only NIC via the `mcos.json` `bindAddress` field.

The Forsetti compliance script does **not** enforce this; it is host-administration policy.

## Validation

After adding the rules, validate from another machine on the LAN:

```powershell
# From another host on the LAN — replace <host-ip> with the MCOS host's IP.
Test-NetConnection -ComputerName <host-ip> -Port 7300   # operator surface
Test-NetConnection -ComputerName <host-ip> -Port 8080   # MCP Gateway (open only when a gateway binary is supervised)
# DNS-SD validation requires Bonjour Browser or `dns-sd -B _mcos._tcp` (macOS),
# `avahi-browse _mcos._tcp` (Linux), or PowerShell:
Resolve-DnsName -Name _mcos._tcp.local -Type PTR -LlmnrFallback
```

The MCP Gateway port (TCP 8080) only has a listener when a gateway substrate is supervising it (PHASE-02 honest-fallback rule from ADR-002 §9). The firewall rule is correct to open the port now — `Test-NetConnection` will report `False` until an `IMcpGateway` adapter is configured. The operator port (TCP 7300) listens unconditionally.

From a Public network, the same calls should fail. If they succeed, the Public profile is too permissive.

## Removing the rules

Uninstalling MCOS does NOT remove the firewall rules (the MSI does not own that policy state). To clean up:

```powershell
Get-NetFirewallRule -DisplayName "MCOS *" | Remove-NetFirewallRule
```

## See also

- [ADR-002 — Gateway-first MCP realignment](../Architecture-Decisions/ADR-002-gateway-first-mcp-realignment.md) — section 1 (trust model), section 3 (LAN discovery), section 10 (release gate)
- [Packaging and Gateway Binary](Packaging-and-Gateway-Binary.md) — what the MSI installs
- [Release Gate](Release-Gate.md) — CI / release flow
