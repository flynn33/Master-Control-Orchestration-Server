---
description: Emit the four New-NetFirewallRule snippets templated with the live ports + check current rule state.
---

Show the operator the firewall snippets needed for LAN advertising, with port values pulled from the live MCOS config.

1. `mcos_config_get` — read live `browserPort`, `beaconPort`, `mcpGateway.listenPort`.
2. `mcos_firewall_check` — see which MCOS rules are already in place.
3. Surface the four snippets with the live ports substituted, in this exact form:

```powershell
# Inbound rule 1 of 4 — MCP Gateway (TCP, AI-client surface)
New-NetFirewallRule `
  -DisplayName "MCOS — MCP Gateway (LAN)" `
  -Direction Inbound `
  -Action Allow `
  -Protocol TCP `
  -LocalPort <gateway listenPort> `
  -Profile Private,Domain `
  -Program "C:\Program Files\Master Control Orchestration Server\MasterControlServiceHost.exe"

# Inbound rule 2 of 4 — Operator surface (TCP, dashboard + admin API)
... (same shape with browserPort)

# Inbound rule 3 of 4 — DNS-SD / mDNS (UDP 5353)
... (UDP 5353)

# Inbound rule 4 of 4 — Discovery beacon (UDP)
... (UDP beaconPort)
```

After the snippets, summarize:

```
CURRENT STATE: <X of 4 rules present and Enabled>
MISSING: <list any that are absent or Disabled>
RUN FROM: elevated PowerShell
PROFILE: Private,Domain (Public is intentionally excluded)
```

If the operator wants to remove all rules:
```powershell
Get-NetFirewallRule -DisplayName 'MCOS *' | Remove-NetFirewallRule
```

This command is read-only on the MCOS side — it doesn't apply rules, it surfaces them for the operator to apply themselves from elevated PowerShell. MCOS does not run elevated; firewall changes are operator-driven.
