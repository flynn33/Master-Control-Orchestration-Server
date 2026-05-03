# Windows Firewall and LAN Mode

PHASE-10 baseline (ADR-002 §1, §10).

## Trust model boundary

MCOS exposes two logically distinct surfaces. Both run on the host but are scoped differently:

| Surface | Audience | Auth | Trust | Where |
|---|---|---|---|---|
| AI-client MCP Gateway | AI clients on the trusted LAN (Claude Code, Codex, Grok, ChatGPT, generic MCP) | `auth=none` | `trust=lan` | Gateway port (default `8080`, configurable in `mcos.json`) at `/mcp` |
| Operator surface | The host operator(s) | LAN-trusted (no app-layer auth per ADR-001 §3) | `trust=lan` | Admin port — the dashboard at `/`, JSON API at `/api/*` |

**The trust boundary is enforced at the network layer, not the application layer.** Windows Firewall is the load-bearing control. Both surfaces refuse to participate when reachable from a Public network profile.

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
  -LocalPort 18443 `
  -Profile Private,Domain `
  -Program "C:\Program Files\Master Control Orchestration Server\MasterControlServiceHost.exe"
```

Adjust `LocalPort` to match the operator HTTP port from `mcos.json`. Same `Profile` constraint.

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
  -LocalPort 24567 `
  -Profile Private,Domain `
  -Program "C:\Program Files\Master Control Orchestration Server\MasterControlServiceHost.exe"
```

The legacy beacon broadcasts on a configurable port (`24567` is the default in `mcos.json`'s `beacon.port`). Newer clients use DNS-SD; this rule keeps the older browser tooling working.

## Public profile blocks

If MCOS is reachable on a Public profile, treat that as a misconfiguration and either:
- Reclassify the connection (Settings → Network → set the network to Private), or
- Bind MCOS to a specific LAN-only NIC via the `mcos.json` `bindAddress` field.

The Forsetti compliance script does **not** enforce this; it is host-administration policy.

## Validation

After adding the rules, validate from another machine on the LAN:

```powershell
# From another host on the LAN — replace <host-ip> with the MCOS host's IP.
Test-NetConnection -ComputerName <host-ip> -Port 8080
Test-NetConnection -ComputerName <host-ip> -Port 18443
# DNS-SD validation requires Bonjour Browser or `dns-sd -B _mcos._tcp` (macOS),
# `avahi-browse _mcos._tcp` (Linux), or PowerShell:
Resolve-DnsName -Name _mcos._tcp.local -Type PTR -LlmnrFallback
```

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
