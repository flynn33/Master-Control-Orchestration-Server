---
description: Register a managed worker pool. Walks through ManagedEndpointPool fields with sensible defaults.
argument-hint: [poolId] [kind: mcp-server|sub-agent]
---

Register a new managed endpoint pool with MCOS via `mcos_pool_upsert`.

Hand off to the `mcos-pool-architect` sub-agent. The sub-agent will:

1. Ask about the workload to pick a sensible scale policy.
2. Ask the operator for the executable path of the worker binary and confirm it exists on disk.
3. Build a complete `ManagedEndpointPool` JSON.
4. Show the operator the diff before applying.
5. `mcos_pool_upsert pool={...}` to register.
6. `mcos_pool_scale poolId=...` to honor `minInstances`.
7. `mcos_pool_get poolId=...` to verify lifecycle progression `Configured -> Starting -> Ready` (or `Failed` — in which case hand off to mcos-troubleshooter).

If $ARGUMENTS are provided, they're hints — `$1` for poolId, `$2` for kind. Use them as starting suggestions; the sub-agent still asks the operator to confirm.

Don't apply without operator confirmation.
