---
description: Quick MCOS status — service, gateway, pools, presence, governance posture, in one view.
---

Run a quick health check against the running MCOS instance. Use the `mcos-bridge` MCP tools.

Pull these in parallel:
1. `mcos_health`
2. `mcos_gateway_status`
3. `mcos_gateway_health`
4. `mcos_pools_list`
5. `mcos_telemetry_clients`
6. `mcos_governance_approvals`

Then summarize in this exact shape:

```
MCOS STATUS · <instanceId from mcos_dashboard if you have it>
─────────────────────────────────────────────────────────────
Service           : <ok | unreachable>
Gateway           : <state> · <health> · <adapter type>
Advertised URL    : <gateway mcpUrl>
Pools             : <N pool(s) · <ready instances> ready · <active leases> active leases>
Connected clients : <count>
Governance        : <posture> · <pending approvals count>
─────────────────────────────────────────────────────────────
```

If anything is unhealthy, append a one-line "ATTENTION:" pointer at what to check next. Keep the whole response under 250 words.
