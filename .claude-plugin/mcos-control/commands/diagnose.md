---
description: Full diagnostic sweep — service, gateway, pools, firewall, DNS-SD, recent error events. Use when something feels off.
---

Run the full diagnostic sweep. This is more thorough than `/mcos:status` — use it when something is misbehaving and you don't know yet what.

Hand off to the `mcos-troubleshooter` sub-agent with the operator's stated symptom (or "general health check" if none was given). The sub-agent will walk the appropriate diagnostic chain.

If the user gave no symptom, run all of these and surface anything anomalous:

1. `mcos_health` — service responsive?
2. `mcos_service_status` — Windows service running?
3. `mcos_gateway_status` + `mcos_gateway_health` — supervised, healthy?
4. `mcos_pools_list`, then for each pool: `mcos_pool_get` and check for any instance with `state=Failed`.
5. `mcos_telemetry_events?max=200` — filter to `severity in (warning, error, critical)`. Surface the last 10 of those.
6. `mcos_firewall_check` — all four MCOS rules present and Enabled?
7. `mcos_dns_sd_check` — DNS-SD advertisement reachable?

Report findings in priority order — blocking issues first, warnings second. For each issue, point at the canonical wiki page (Troubleshooting, Gateway, LAN-Discovery, etc.).
